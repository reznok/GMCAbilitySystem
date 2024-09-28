#pragma once

#include "CoreMinimal.h"
#include "Utility/GMASBoundQueue.h"
#include "GMCAbilityOuterApplication.generated.h"

class UGMCAbilityEffect;

USTRUCT(BlueprintType)
struct FGMCAbilityEffectRPCWrapper
{
	GENERATED_BODY()

	UPROPERTY()
	FGMASBoundQueueRPCHeader Header;
	
	UPROPERTY()
	FGMCAbilityEffectData Payload {};
};

USTRUCT(BlueprintType)
struct FGMCAbilityEffectIdSet
{
	GENERATED_BODY()
	
	TArray<int> Ids = {};
};

