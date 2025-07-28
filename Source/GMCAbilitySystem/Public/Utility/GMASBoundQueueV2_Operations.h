#pragma once
#include "GameplayTagContainer.h"
#include "InputAction.h"
#include "Effects/GMCAbilityEffect.h"
#include "GMASBoundQueueV2_Operations.generated.h"

USTRUCT()
struct FGMASBoundQueueV2OperationBaseData
{
	GENERATED_BODY()

	FGMASBoundQueueV2OperationBaseData(){}

	// The ID of the operation
	// If this is a server operation, this will be a positive number
	// If this is a client operation, this will be a negative number
	// If a move contains a positive ID, it means the client is ACKing the server operation
	// If a move contains a negative ID, it means the client is providing operation data (ie: Ability Activation)
	UPROPERTY()
	int32 OperationID { 0 };
};


USTRUCT()
struct FGMASBoundQueueV2AbilityActivationOperation : public FGMASBoundQueueV2OperationBaseData
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

USTRUCT()
struct FGMASBoundQueueV2EffectApplicationOperation : public FGMASBoundQueueV2OperationBaseData
{
	GENERATED_BODY()

	UPROPERTY()
	int EffectID {-1};

	UPROPERTY()
	TSubclassOf<UGMCAbilityEffect> EffectClass;

	UPROPERTY()
	FGMCAbilityEffectData EffectData;
};