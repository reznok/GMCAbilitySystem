// Minimal stub movement component for Layer-2 GMAS automation tests.
//
// Created via NewObject<> with no owner actor.  UActorComponent::GetNetMode()
// returns NM_Standalone when there is no owner, satisfying all
// BoundQueueV2 / GenPredictionTick internal checks without any overrides.
//
// GMC BindSinglePrecisionFloat / BindGameplayTagContainer / BindInstancedStruct
// calls in BindReplicationData() populate AliasData arrays on this object and
// are never consumed — no move loop means no crash and no observable side-effects.

#pragma once

#include "CoreMinimal.h"
#include "GMCMovementUtilityComponent.h"
#include "UGMAS_TestMovementCmp.generated.h"

UCLASS(NotBlueprintable, NotBlueprintType)
class UGMAS_TestMovementCmp : public UGMC_MovementUtilityCmp
{
	GENERATED_BODY()
};
