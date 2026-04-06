// Minimal ability stub for Layer-2 GMAS automation tests.
//
// Properties (AbilityTag, CooldownTime, bAllowMultipleInstances, etc.) are
// configured per-test by writing to the CDO via
// GetMutableDefault<UGMAS_TestAbility>() in the spec's SetupHarness /
// TeardownHarness so that each test starts from a known state.

#pragma once

#include "CoreMinimal.h"
#include "Ability/GMCAbility.h"
#include "UGMAS_TestAbility.generated.h"

UCLASS(NotBlueprintable, NotBlueprintType)
class UGMAS_TestAbility : public UGMCAbility
{
	GENERATED_BODY()
};
