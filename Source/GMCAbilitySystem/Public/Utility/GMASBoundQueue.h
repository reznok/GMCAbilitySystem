// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GMCMovementUtilityComponent.h"
#include "InstancedStruct.h"
#include "UObject/Object.h"
#include "UObject/ConstructorHelpers.h"
#include "GMASBoundQueue.generated.h"

UENUM(BlueprintType)
enum class EGMASBoundQueueOperationType : uint8
{
	None,
	Add,
	Remove,
	Activate,
	Cancel
};

USTRUCT(BlueprintType)
struct FGMASBoundQueueOperationIdSet
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<int> Ids = {};
};

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FGMASBoundQueueRPCHeader
{
	GENERATED_BODY()

	UPROPERTY()
	int32 OperationId { -1 };

	UPROPERTY()
	uint8 OperationTypeRaw { 0 };

	EGMASBoundQueueOperationType GetOperationType() const
	{
		return static_cast<EGMASBoundQueueOperationType>(OperationTypeRaw);
	}
	
	UPROPERTY()
	FGameplayTag Tag { FGameplayTag::EmptyTag };

	UPROPERTY()
	FName ItemClassName { NAME_None };

	UPROPERTY()
	FGMASBoundQueueOperationIdSet PayloadIds {};

	UPROPERTY()
	FInstancedStruct InstancedPayload {};

	UPROPERTY()
	float RPCGracePeriodSeconds { 1.f };

	UPROPERTY()
	uint8 ExtraFlags { 0 };
};

template<typename C, typename T>
struct GMCABILITYSYSTEM_API TGMASBoundQueueOperation
{
	FGMASBoundQueueRPCHeader Header {};
	
	// An actual class to be utilized with this, in case we need to instance it.
	TSubclassOf<C> ItemClass { nullptr };

	FInstancedStruct InstancedPayloadIds;
	
	// The struct payload for this item.
	T Payload;	

	// The instanced struct representation of this payload, used to actually
	// bind for replication.
	FInstancedStruct InstancedPayload;

	EGMASBoundQueueOperationType GetOperationType() const
	{
		return Header.GetOperationType();
	}

	void SetOperationType(EGMASBoundQueueOperationType OperationType)
	{
		Header.OperationTypeRaw = static_cast<uint8>(OperationType);
	}

	int32 GetOperationId() const { return Header.OperationId; }

	TArray<int32> GetPayloadIds() const { return Header.PayloadIds.Ids; }

	FGameplayTag GetTag() const { return Header.Tag; }

	bool GracePeriodExpired() const
	{
		return Header.RPCGracePeriodSeconds <= 0.f;
	}

	bool IsValid() const
	{
		return Header.OperationTypeRaw != 0 && (Header.ItemClassName != NAME_None || Header.Tag != FGameplayTag::EmptyTag || Header.PayloadIds.Ids.Num() > 0);
	}

	void Refresh(bool bDecodePayload = false)
	{
		if (bDecodePayload)
		{
			// Incoming from remote.
			Payload = Header.InstancedPayload.Get<T>();
			if (Header.PayloadIds.Ids.Num() == 0 && InstancedPayloadIds.IsValid())
			{
				Header.PayloadIds = InstancedPayloadIds.Get<FGMASBoundQueueOperationIdSet>();
			}
		}
		else
		{
			// Outgoing
			Header.InstancedPayload = FInstancedStruct::Make<T>(Payload);
			InstancedPayloadIds = FInstancedStruct::Make<FGMASBoundQueueOperationIdSet>(Header.PayloadIds);
		}

		RefreshClass();
	}

	void RefreshClass()
	{
		if (Header.ItemClassName != NAME_None && !ItemClass)
		{
			// Get a handle to our class, for instancing purposes.
			TSoftClassPtr<C> ClassPtr = TSoftClassPtr<C>(FSoftObjectPath(Header.ItemClassName.ToString()));
			ItemClass = ClassPtr.LoadSynchronous();
		}

		if (ItemClass && Header.ItemClassName == NAME_None)
		{
			Header.ItemClassName = FName(ItemClass->GetPathName());
		}
	}
	
};

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FGMASBoundQueueAcknowledgement
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Id { -1 };

	UPROPERTY()
	float Lifetime { 5.f };
};

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FGMASBoundQueueAcknowledgements
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FGMASBoundQueueAcknowledgement> AckSet;
};

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FGMASBoundQueueEmptyData
{
	GENERATED_BODY()
};

template<typename C, typename T, bool ClientAuth = true>
class GMCABILITYSYSTEM_API TGMASBoundQueue
{

public:

	TGMASBoundQueueOperation<C, T> CurrentOperation;

	int BI_ActionTimer { -1 };
	int BI_Acknowledgements { -1 };
	int BI_OperationId { -1 };
	int BI_OperationType { -1 };
	int BI_OperationTag { -1 };
	int BI_OperationClass { -1 };
	int BI_OperationExtraFlags { -1 };
	int BI_OperationPayload { -1 };
	int BI_OperationPayloadIds { -1 };
	
	TArray<TGMASBoundQueueOperation<C, T>> QueuedBoundOperations;
	TArray<TGMASBoundQueueOperation<C, T>> QueuedRPCOperations;

	FInstancedStruct Acknowledgments;

	double ActionTimer { 0 };

	void BindToGMC(UGMC_MovementUtilityCmp* MovementComponent)
	{
		const EGMC_PredictionMode Prediction = ClientAuth ? EGMC_PredictionMode::ClientAuth_Input : EGMC_PredictionMode::ServerAuth_Input_ClientValidated;
		const EGMC_PredictionMode AckPrediction = ClientAuth ? EGMC_PredictionMode::ServerAuth_Output_ClientValidated : EGMC_PredictionMode::ClientAuth_Input;
		
		Acknowledgments = FInstancedStruct::Make<FGMASBoundQueueAcknowledgements>(FGMASBoundQueueAcknowledgements());

		// Our queue's action timer is always server-auth.
		BI_ActionTimer = MovementComponent->BindDoublePrecisionFloat(
			ActionTimer,
			EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
			EGMC_CombineMode::CombineIfUnchanged,
			EGMC_SimulationMode::None,
			EGMC_InterpolationFunction::TargetValue
			);

		if (!ClientAuth)
		{
			// Acknowledgements are bound client-auth for server-auth effects.
			BI_Acknowledgements = MovementComponent->BindInstancedStruct(
				Acknowledgments,
				AckPrediction,
				EGMC_CombineMode::CombineIfUnchanged,
				EGMC_SimulationMode::None,
				EGMC_InterpolationFunction::TargetValue);
		}
		else
		{
			// For client-auth stuff, we bind the individual pieces of the queue.
			// We will probably not use this often (if ever), but it exists just-in-case.
			
			BI_OperationId = MovementComponent->BindInt(
				CurrentOperation.Header.OperationId,
				Prediction,
				EGMC_CombineMode::CombineIfUnchanged,
				EGMC_SimulationMode::Periodic_Output,
				EGMC_InterpolationFunction::TargetValue);

			BI_OperationType = MovementComponent->BindByte(
				CurrentOperation.Header.OperationTypeRaw,
				Prediction,
				EGMC_CombineMode::CombineIfUnchanged,
				EGMC_SimulationMode::Periodic_Output,
				EGMC_InterpolationFunction::TargetValue);

			BI_OperationTag = MovementComponent->BindGameplayTag(
				CurrentOperation.Header.Tag,
				Prediction,
				EGMC_CombineMode::CombineIfUnchanged,
				EGMC_SimulationMode::Periodic_Output,
				EGMC_InterpolationFunction::TargetValue);

			BI_OperationClass = MovementComponent->BindName(
				CurrentOperation.Header.ItemClassName,
				Prediction,
				EGMC_CombineMode::CombineIfUnchanged,
				EGMC_SimulationMode::Periodic_Output,
				EGMC_InterpolationFunction::TargetValue);

			BI_OperationPayload = MovementComponent->BindInstancedStruct(
				CurrentOperation.Header.InstancedPayload,
				Prediction,
				EGMC_CombineMode::CombineIfUnchanged,
				EGMC_SimulationMode::Periodic_Output,
				EGMC_InterpolationFunction::TargetValue);

			BI_OperationExtraFlags = MovementComponent->BindByte(
				CurrentOperation.Header.ExtraFlags,
				Prediction,
				EGMC_CombineMode::CombineIfUnchanged,
				EGMC_SimulationMode::Periodic_Output,
				EGMC_InterpolationFunction::TargetValue);

			BI_OperationPayloadIds = MovementComponent->BindInstancedStruct(
				CurrentOperation.InstancedPayloadIds,
				Prediction,
				EGMC_CombineMode::CombineIfUnchanged,
				EGMC_SimulationMode::Periodic_Output,
				EGMC_InterpolationFunction::TargetValue);
		}

		CurrentOperation = TGMASBoundQueueOperation<C, T>();
		CurrentOperation.Header = FGMASBoundQueueRPCHeader();
		CurrentOperation.Refresh(false);
	}
	
	void PreLocalMovement()
	{
		if (QueuedBoundOperations.Num() > 0 && ClientAuth)
		{
			CurrentOperation = QueuedBoundOperations.Pop();
			CurrentOperation.Refresh(false);
		}
	}

	void PreRemoteMovement()
	{
		ClearCurrentOperation();
		if (QueuedBoundOperations.Num() > 0 && !ClientAuth)
		{
			CurrentOperation = QueuedBoundOperations.Pop();
			CurrentOperation.Refresh(false);
		}
	}

	int32 GenerateOperationId() const
	{
		int32 NewOperationId = ActionTimer * 100;

		TGMASBoundQueueOperation<C, T> Operation;
		
		while (GetOperationById(NewOperationId, Operation))
		{
			NewOperationId++;
		}
		return NewOperationId;
	}
	
	void ClearCurrentOperation()
	{
		CurrentOperation.Header = FGMASBoundQueueRPCHeader();
		
		// CurrentOperation.Header.OperationId = -1;
		// CurrentOperation.Header.Tag = FGameplayTag::EmptyTag;
		// CurrentOperation.Header.ItemClassName = NAME_None;
		// CurrentOperation.SetOperationType(EGMASBoundQueueOperationType::None);
		// CurrentOperation.Header.PayloadIds.Ids.Empty();
		
		CurrentOperation.Payload = T();
		CurrentOperation.Refresh(false);
	}

	void GenPredictionTick(float DeltaTime)
	{
		ActionTimer += DeltaTime;
		ExpireStaleAcks(DeltaTime);
	}

	int32 MakeOperation(TGMASBoundQueueOperation<C, T>& NewOperation, EGMASBoundQueueOperationType Type, FGameplayTag Tag, const T& Payload, TArray<int> PayloadIds = {}, TSubclassOf<C> ItemClass = nullptr, float RPCGracePeriod = 1.f, uint8 ExtraFlags = 0)
	{
		NewOperation.Header.OperationId = GenerateOperationId();
		NewOperation.SetOperationType(Type);
		NewOperation.Header.Tag = Tag;
		NewOperation.Payload = Payload;
		NewOperation.ItemClass = ItemClass;
		NewOperation.Header.RPCGracePeriodSeconds = RPCGracePeriod;
		NewOperation.Header.PayloadIds.Ids = PayloadIds;
		NewOperation.Header.ExtraFlags = ExtraFlags;

		NewOperation.Refresh(false);
		
		return NewOperation.GetOperationId();
	}

	int32 MakeOperation(TGMASBoundQueueOperation<C, T>& NewOperation, const FGMASBoundQueueRPCHeader& Header, const T& Payload)
	{
		NewOperation.Header = Header;
		NewOperation.Payload = Payload;
		NewOperation.Refresh();
		NewOperation.InstancedPayload = FInstancedStruct::Make<T>(Payload);
		NewOperation.InstancedPayloadIds = FInstancedStruct::Make<FGMASBoundQueueOperationIdSet>(NewOperation.Header.PayloadIds);

		return NewOperation.GetOperationId();
	}

	void QueuePreparedOperation(TGMASBoundQueueOperation<C, T>& NewOperation, bool bMovementSynced = true)
	{
		TGMASBoundQueueOperation<C, T> TestOperation = TGMASBoundQueueOperation<C, T>();

		// Don't bother queueing it if it already exists.
		if (GetOperationById(NewOperation.GetOperationId(), TestOperation)) return;
		
		if (bMovementSynced)
		{
			// This needs to be handled via GMC, so add it to our queue.
			QueuedBoundOperations.Push(NewOperation);
		}
		else
		{
			QueuedRPCOperations.Push(NewOperation);
		}
	}
	
	int32 QueueOperation(TGMASBoundQueueOperation<C, T>& NewOperation, EGMASBoundQueueOperationType Type, FGameplayTag Tag, const T& Payload, TArray<int> PayloadIds = {}, TSubclassOf<C> ItemClass = nullptr, bool bMovementSynced = true, float RPCGracePeriod = 1.f)
	{
		MakeOperation(NewOperation, Type, Tag, Payload, PayloadIds, ItemClass, RPCGracePeriod);
		QueuePreparedOperation(NewOperation, bMovementSynced);		
		return NewOperation.GetOperationId();
	}

	void QueueOperationFromHeader(const FGMASBoundQueueRPCHeader& Header, bool bMovementSynced)
	{
		TGMASBoundQueueOperation<C, T> NewOperation;

		NewOperation.Header = Header;
		NewOperation.Refresh(true);
		QueuePreparedOperation(NewOperation, bMovementSynced);
	}

	int Num() const
	{
		return QueuedBoundOperations.Num();
	}

	int NumMatching(FGameplayTag Tag, EGMASBoundQueueOperationType Type = EGMASBoundQueueOperationType::None) const
	{
		int Result = 0;
		for (const auto& Operation : QueuedBoundOperations)
		{
			if (Operation.GetTag() == Tag)
			{
				if (Type == EGMASBoundQueueOperationType::None || Operation.GetOperationType() == Type) Result++;
			}
		}
		for (const auto& Operation : QueuedRPCOperations)
		{
			if (Operation.GetTag() == Tag)
			{
				if (Type == EGMASBoundQueueOperationType::None || Operation.GetOperationType() == Type) Result++;
			}
		}		
		return Result;
	}

	const TArray<TGMASBoundQueueOperation<C, T>>& GetQueuedOperations() const { return QueuedBoundOperations; }

	const TArray<TGMASBoundQueueOperation<C, T>>& GetQueuedRPCOperations() const { return QueuedRPCOperations; }

	bool GetOperationById(int32 OperationId, TGMASBoundQueueOperation<C, T>& OutOperation) const
	{
		for (const auto& Operation : GetQueuedOperations())
		{
			if (Operation.GetOperationId() == OperationId)
			{
				OutOperation = Operation;
				return true;
			}
		}

		for (const auto& Operation : GetQueuedRPCOperations())
		{
			if (Operation.GetOperationId() == OperationId)
			{
				OutOperation = Operation;
				return true;
			}
		}

		return false;
	}

	bool HasOperationWithPayloadId(int32 PayloadId) const
	{
		for (const auto& Operation : QueuedBoundOperations)
		{
			if (Operation.GetPayloadIds().Contains(PayloadId))
			{
				return true;
			}
		}

		for (const auto& Operation : QueuedRPCOperations)
		{
			if (Operation.GetPayloadIds().Contains(PayloadId))
			{
				return true;
			}
		}

		return false;		
	}

	bool RemoveOperationById(int32 OperationId)
	{
		int TargetIdx = -1;

		for (int Idx = 0; Idx < QueuedRPCOperations.Num() && TargetIdx == -1; Idx++)
		{
			if(QueuedRPCOperations[Idx].GetOperationId() == OperationId)
			{
				TargetIdx = Idx;
			}
		}

		if (TargetIdx != -1)
		{
			QueuedRPCOperations.RemoveAtSwap(TargetIdx, 1, false);
			return true;
		}

		TargetIdx = -1;
		for (int Idx = 0; Idx < QueuedBoundOperations.Num() && TargetIdx == -1; Idx++)
		{
			if(QueuedBoundOperations[Idx].GetOperationId() == OperationId)
			{
				TargetIdx = Idx;
			}
		}
		
		if (TargetIdx != -1)
		{
			QueuedBoundOperations.RemoveAtSwap(TargetIdx, 1, false);
			return true;
		}

		return false;
	}
	
	bool GetCurrentBoundOperation(TGMASBoundQueueOperation<C, T>& Operation, bool bRefresh = false)
	{
		Operation = CurrentOperation;
		if (Operation.GetOperationType() != EGMASBoundQueueOperationType::None)
		{
			if (bRefresh)
			{
				Operation.Refresh(true);
			}
			else
			{
				Operation.RefreshClass();
			}
			return true;
		}

		return false;
	}

	bool PopNextRPCOperation(TGMASBoundQueueOperation<C, T>& Operation)
	{
		if (QueuedRPCOperations.Num() == 0) return false;

		Operation = QueuedRPCOperations.Pop();
		return true;
	}

	void DeductGracePeriod(float DeltaTime)
	{
		for (auto& Operation : QueuedRPCOperations)
		{
			Operation.Header.RPCGracePeriodSeconds -= DeltaTime;
		}
	}

	void Acknowledge(int32 OperationId, float AckLifetime = 5.f)
	{
		if (!IsAcknowledged(OperationId))
		{
			FGMASBoundQueueAcknowledgement NewAck;
			NewAck.Id = OperationId;
			NewAck.Lifetime = AckLifetime;

			auto& Acks = Acknowledgments.GetMutable<FGMASBoundQueueAcknowledgements>();
			Acks.AckSet.Add(NewAck);
		}
	}

	bool IsAcknowledged(int32 OperationId) const
	{
		const auto& Acks = Acknowledgments.Get<FGMASBoundQueueAcknowledgements>();
		for (const auto& Ack : Acks.AckSet)
		{
			if (Ack.Id == OperationId) return true;
		}
		return false;
	}

	void ExpireStaleAcks(float DeltaTime)
	{
		// Deduct from our ack lifetime; if we've gone stale, remove the stale acks to avoid it just growing forever.
		TArray<FGMASBoundQueueAcknowledgement> FreshAcks;
		auto& Acks = Acknowledgments.GetMutable<FGMASBoundQueueAcknowledgements>();
		for (auto& Ack : Acks.AckSet)
		{
			Ack.Lifetime -= DeltaTime;
			if (Ack.Lifetime > 0.f)
			{
				FreshAcks.Add(Ack);
			}
		}
		Acks.AckSet = FreshAcks;
	}
	
};
