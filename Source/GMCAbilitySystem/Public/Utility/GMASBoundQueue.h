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
	Cancel
}; 

template<typename C, typename T>
struct GMCABILITYSYSTEM_API TGMASBoundQueueOperation
{
	// A unique ID for this operation.
	int32 OperationId { -1 };

	uint8 OperationTypeRaw { 0 };
	
	// The tag associated with this operation (if any)
	FGameplayTag Tag { FGameplayTag::EmptyTag };

	// An actual class to be utilized with this, in case we need to instance it.
	TSubclassOf<C> ItemClass { nullptr };

	// An FName representation of this item, used for GMC replication.
	FName ItemClassName { NAME_None };

	// The struct payload for this item.
	T Payload;	

	// The instanced struct representation of this payload, used to actually
	// bind for replication.
	FInstancedStruct InstancedPayload;

	// If true, this item must be replicated via GMC and thus preserved in the
	// movement history. If false, this item can be sent via standard Unreal RPC
	// separate from GMC.
	bool bMovementSync { true };

	EGMASBoundQueueOperationType GetOperationType() const
	{
		return static_cast<EGMASBoundQueueOperationType>(OperationTypeRaw);
	}

	void SetOperationType(EGMASBoundQueueOperationType OperationType)
	{
		OperationTypeRaw = static_cast<uint8>(OperationType);
	}
	
};

template<typename C, typename T, bool ClientAuth = true>
class GMCABILITYSYSTEM_API TGMASBoundQueue
{

public:

	TGMASBoundQueueOperation<C, T> CurrentOperation;

	int BI_OperationId { -1 };
	int BI_OperationType { -1 };
	int BI_OperationTag { -1 };
	int BI_OperationClass { -1 };
	int BI_OperationPayload { -1 };
	
	TArray<TGMASBoundQueueOperation<C, T>> QueuedOperations;

	double ActionTimer { 0 };

	void BindToGMC(UGMC_MovementUtilityCmp* MovementComponent)
	{
		const EGMC_PredictionMode Prediction = ClientAuth ? EGMC_PredictionMode::ClientAuth_Input : EGMC_PredictionMode::ServerAuth_Output_ClientValidated;

		BI_OperationId = MovementComponent->BindInt(
			CurrentOperation.OperationId,
			Prediction,
			EGMC_CombineMode::CombineIfUnchanged,
			EGMC_SimulationMode::Periodic_Output,
			EGMC_InterpolationFunction::TargetValue);

		BI_OperationType = MovementComponent->BindByte(
			CurrentOperation.OperationTypeRaw,
			Prediction,
			EGMC_CombineMode::CombineIfUnchanged,
			EGMC_SimulationMode::Periodic_Output,
			EGMC_InterpolationFunction::TargetValue);

		BI_OperationTag = MovementComponent->BindGameplayTag(
			CurrentOperation.Tag,
			Prediction,
			EGMC_CombineMode::CombineIfUnchanged,
			EGMC_SimulationMode::Periodic_Output,
			EGMC_InterpolationFunction::TargetValue);

		BI_OperationClass = MovementComponent->BindName(
			CurrentOperation.ItemClassName,
			Prediction,
			EGMC_CombineMode::CombineIfUnchanged,
			EGMC_SimulationMode::Periodic_Output,
			EGMC_InterpolationFunction::TargetValue);

		BI_OperationPayload = MovementComponent->BindInstancedStruct(
			CurrentOperation.InstancedPayload,
			Prediction,
			EGMC_CombineMode::CombineIfUnchanged,
			EGMC_SimulationMode::Periodic_Output,
			EGMC_InterpolationFunction::TargetValue);
	}

	void PreLocalMovement()
	{
		if (QueuedOperations.Num() > 0)
		{
			CurrentOperation = QueuedOperations.Pop();
		}
	}

	int32 GenerateOperationId() const { return ActionTimer * 100; }
	
	void ClearCurrentOperation()
	{
		CurrentOperation.OperationId = -1;
		CurrentOperation.Tag = FGameplayTag::EmptyTag;
		CurrentOperation.ItemClassName = NAME_None;
		CurrentOperation.SetOperationType(EGMASBoundQueueOperationType::None);
	}

	void GenPredictionTick(float DeltaTime)
	{
		ActionTimer += DeltaTime;
	}

	int32 QueueOperation(TGMASBoundQueueOperation<C, T>& NewOperation, EGMASBoundQueueOperationType Type, FGameplayTag Tag, const T& Payload, TSubclassOf<C> ItemClass = nullptr, bool bMovementSynced = true)
	{
		NewOperation.OperationId = GenerateOperationId();
		NewOperation.SetOperationType(Type);
		NewOperation.Tag = Tag;
		NewOperation.Payload = Payload;
		NewOperation.InstancedPayload = FInstancedStruct::Make<T>(Payload);
		NewOperation.ItemClass = ItemClass;
		if (ItemClass)
		{
			NewOperation.ItemClassName = ItemClass->GetFullName();
		}

		if (bMovementSynced)
		{
			// This needs to be handled via GMC, so add it to our queue.
			QueuedOperations.Push(NewOperation);
		}

		return NewOperation.OperationId;
	}

	int Num() const
	{
		return QueuedOperations.Num();
	}
	
	bool GetCurrentOperation(TGMASBoundQueueOperation<C, T>& Operation)
	{
		Operation = CurrentOperation;

		if (Operation.GetOperationType() != EGMASBoundQueueOperationType::None)
		{
			Operation.Payload = CurrentOperation.InstancedPayload.template Get<T>();

			if (Operation.ItemClassName != NAME_None)
			{
				// Get a handle to our class, for instancing purposes.
				ConstructorHelpers::FClassFinder<C> ResolvedItemClass(*Operation.ItemClassName.ToString());
				if (ResolvedItemClass.Class != nullptr)
				{
					Operation.ItemClass = ResolvedItemClass.Class;
				}
			}
			return true;
		}

		return false;
	}
	
};
