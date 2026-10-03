// Minimal effect stub used by Layer-2 cost-affordability tests.
// EffectData.Modifiers is populated per-test by writing to the CDO via
// GetMutableDefault<UGMAS_TestCostEffect>().

#pragma once

#include "CoreMinimal.h"
#include "Effects/GMCAbilityEffect.h"
#include "UGMAS_TestCostEffect.generated.h"

UCLASS(NotBlueprintable, NotBlueprintType)
class UGMAS_TestCostEffect : public UGMCAbilityEffect
{
	GENERATED_BODY()
};
