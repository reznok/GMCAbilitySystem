#pragma once
#include "GameplayTagContainer.h"
#include "InputAction.h"
#include "Effects/GMCAbilityEffect.h"
#include "InstancedStruct.h"
#include "GMASBoundQueueV2_Operations.generated.h"

USTRUCT()
struct GMCABILITYSYSTEM_API FGMASBoundQueueV2OperationBaseData
{
	GENERATED_BODY()

	FGMASBoundQueueV2OperationBaseData(){}
	FGMASBoundQueueV2OperationBaseData(const int ID){OperationID = ID;}

	// The ID of the operation
	// If this is a server operation, this will be a positive number
	// If this is a client operation, this will be a negative number
	// If a move contains a positive ID, it means the client is ACKing the server operation
	// If a move contains a negative ID, it means the client is providing operation data (ie: Ability Activation)
	UPROPERTY()
	int32 OperationID { 0 };
};



// Client Auth Operations
// UNTRUSTED DATA!!!
// Operations sent by clients to the server to request an action.
// Currently used for Ability Activation but can be extended for other actions
// Make sure to add operations here to the ValidClientInputOperationTypes in GMASBoundQueueV2.h

USTRUCT()
struct GMCABILITYSYSTEM_API FGMASBoundQueueV2AbilityActivationOperation : public FGMASBoundQueueV2OperationBaseData
{
	GENERATED_BODY()
	
	FGMASBoundQueueV2AbilityActivationOperation()
		: InputTag(FGameplayTag::EmptyTag), InputAction(nullptr)
	{
	}
	UPROPERTY()
	FGameplayTag InputTag;

	UPROPERTY()
	const UInputAction* InputAction;
};

// Operation sent by clients when they process server-auth events. Technically a client operation.
USTRUCT()
struct GMCABILITYSYSTEM_API FGMASBoundQueueV2AcknowledgeOperation: public FGMASBoundQueueV2OperationBaseData
{
	GENERATED_BODY()
};


// Server Auth Operations
// Operations generated on the server that need to be sync-executed on clients. They are RPC'd to clients.
// A client should never send these operations to the server as part of their OperationData Output (bound var)
// Currently these will always happen during Prediction tick
// Examples: Apply effect, add impulse, server-auth events, etc.

// Apply Effect
USTRUCT()
struct GMCABILITYSYSTEM_API FGMASBoundQueueV2ApplyEffectOperation : public FGMASBoundQueueV2OperationBaseData
{
	GENERATED_BODY()

	UPROPERTY()
	int EffectID {-1};

	UPROPERTY()
	TSubclassOf<UGMCAbilityEffect> EffectClass;

	UPROPERTY()
	FGMCAbilityEffectData EffectData; 
};

// Remove Effect
USTRUCT()
struct GMCABILITYSYSTEM_API FGMASBoundQueueV2RemoveEffectOperation : public FGMASBoundQueueV2OperationBaseData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<int> EffectIDs{};
};

// Add Impulse
USTRUCT()
struct GMCABILITYSYSTEM_API FGMASBoundQueueV2AddImpulseOperation : public FGMASBoundQueueV2OperationBaseData
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Impulse {FVector::Zero()};

	UPROPERTY()
	bool bVelocityChange {false};
};

// Custom Events (Replaces Synced-Events)
// Use a Tag and whatever payload you want to send
USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FGMASBoundQueueV2CustomEventOperation : public FGMASBoundQueueV2OperationBaseData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "GMASSyncedEvent")
	FGameplayTag EventTag;

	UPROPERTY(BlueprintReadWrite, Category = "GMASSyncedEvent")
	FInstancedStruct InstancedPayload;
};
