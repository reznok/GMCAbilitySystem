#pragma once
#include "GameplayTagContainer.h"
#include "InputAction.h"
#include "Effects/GMCAbilityEffect.h"
#include "InstancedStruct.h"
#include "GMASBoundQueueV2_Operations.generated.h"

class UGMCAbility;

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

// Client-auth ability activation. Sent by clients for abilities present in
// UGMC_AbilitySystemComponent::ClientAuthorizedAbilities. Server trusts the
// activation request without running ActivationRequiredTags / ActivationBlockedTags
// gates, but still honors BlockedByOtherAbility and Cooldown for runtime coherence.
//
// UNTRUSTED — treat AbilityClass as untrusted client input. The server WILL verify
// that the class is whitelisted before accepting. Without that check, a malicious
// client could activate any granted ability.
USTRUCT()
struct GMCABILITYSYSTEM_API FGMASBoundQueueV2ClientAuthAbilityActivationOperation : public FGMASBoundQueueV2OperationBaseData
{
	GENERATED_BODY()

	FGMASBoundQueueV2ClientAuthAbilityActivationOperation()
		: AbilityClass(nullptr)
		, InputTag(FGameplayTag::EmptyTag)
		, InputAction(nullptr)
	{
	}

	UPROPERTY()
	TSubclassOf<UGMCAbility> AbilityClass;

	UPROPERTY()
	FGameplayTag InputTag;

	UPROPERTY()
	const UInputAction* InputAction;
};

// Client-auth effect application. Sent by clients for effects present in
// UGMC_AbilitySystemComponent::ClientAuthorizedAbilityEffects. Server trusts the
// apply request without running ApplicationMustHaveTags / ActivationQuery gates,
// but still honors MustMaintainQuery / MustHave/MustNotHaveTags during the
// effect's lifetime.
//
// UNTRUSTED — server-side whitelist check + EffectID range check are mandatory
// before apply.
USTRUCT()
struct GMCABILITYSYSTEM_API FGMASBoundQueueV2ClientAuthEffectOperation : public FGMASBoundQueueV2OperationBaseData
{
	GENERATED_BODY()

	FGMASBoundQueueV2ClientAuthEffectOperation()
		: EffectClass(nullptr)
		, EffectID(-1)
	{
	}

	UPROPERTY()
	TSubclassOf<UGMCAbilityEffect> EffectClass;

	UPROPERTY()
	int EffectID;

	UPROPERTY()
	FGMCAbilityEffectData EffectData;
};

// Client-auth effect removal. Mirror of FGMASBoundQueueV2ClientAuthEffectOperation
// for the remove path. Server validates that all EffectIDs are in the client-auth
// reserved range before applying the removal.
//
// UNTRUSTED — server MUST verify the IDs were originally allocated through the
// client-auth path (i.e. >= ClientAuthEffectIDOffset). Without this check, a
// malicious client could request removal of any active effect by spoofing IDs.
USTRUCT()
struct GMCABILITYSYSTEM_API FGMASBoundQueueV2ClientAuthRemoveEffectOperation : public FGMASBoundQueueV2OperationBaseData
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<int> EffectIDs;
};

// Operation sent by clients when they process server-auth events. Technically a client operation.
USTRUCT()
struct GMCABILITYSYSTEM_API FGMASBoundQueueV2AcknowledgeOperation: public FGMASBoundQueueV2OperationBaseData
{
	GENERATED_BODY()
};

// Server-broadcasted batch wrapper. Carries N OperationIDs from the server's
// QueueServerOperation calls in a single client move payload. Built by
// GenPreLocalMoveExecution when ClientQueuedOperations holds 2+ entries.
// Sub-payloads remain individually cached in OperationPayloads; the wrapper
// itself carries no payload (OperationID stays 0).
//
// IMPORTANT: NOT in ValidClientInputOperationTypes. ServerProcessOperation
// must reject this struct if it ever appears in a client move payload --
// only the server populates this slot, never the client direction.
USTRUCT()
struct GMCABILITYSYSTEM_API FGMASBoundQueueV2BatchOperation : public FGMASBoundQueueV2OperationBaseData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<int32> SubOperationIDs;
};

// Client-sent batch ack. Confirms N ops the server previously broadcast via
// FGMASBoundQueueV2BatchOperation. Server iterates AcknowledgedIDs and calls
// ServerProcessAcknowledgedOperation per ID, removing each from the grace map.
USTRUCT()
struct GMCABILITYSYSTEM_API FGMASBoundQueueV2BatchAcknowledgeOperation : public FGMASBoundQueueV2OperationBaseData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<int32> AcknowledgedIDs;
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

// Set Actor Location (Teleport)
USTRUCT()
struct GMCABILITYSYSTEM_API FGMASBoundQueueV2SetActorLocationOperation : public FGMASBoundQueueV2OperationBaseData
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Location {FVector::Zero()};

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
