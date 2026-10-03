// Second minimal ability stub for Layer-2 GMAS automation tests.
//
// Used alongside UGMAS_TestAbility to test inter-ability interactions:
// BlockOtherAbility, BlockedByOtherAbility, and CancelAbilitiesWithTag.
//
// All properties are configured per-test via GetMutableDefault<UGMAS_TestAbilityB>()
// and reset in TeardownHarness so tests do not bleed into each other.

#pragma once

#include "CoreMinimal.h"
#include "Ability/GMCAbility.h"
#include "UGMAS_TestAbilityB.generated.h"

UCLASS(NotBlueprintable, NotBlueprintType)
class UGMAS_TestAbilityB : public UGMCAbility
{
	GENERATED_BODY()
};
