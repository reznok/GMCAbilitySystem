// Minimal ability stub for Layer-2 GMAS automation tests.
//
// Properties (AbilityTag, CooldownTime, bAllowMultipleInstances, etc.) are
// configured per-test by writing to the CDO via
// GetMutableDefault<UGMAS_TestAbility>() in the spec's SetupHarness /
// TeardownHarness so that each test starts from a known state. Instances count
// their own begin / end / cancel events so the end-path specs can tell a
// natural end from a cancel.

#pragma once

#include "CoreMinimal.h"
#include "Ability/GMCAbility.h"
#include "UGMAS_TestAbility.generated.h"

UCLASS(NotBlueprintable, NotBlueprintType)
class UGMAS_TestAbility : public UGMCAbility
{
	GENERATED_BODY()

public:
	// Per-instance event counters for the end-path and cost specs.
	UPROPERTY() int BeginAbilityEventCount = 0;
	UPROPERTY() int EndAbilityEventCount = 0;
	UPROPERTY() int CancelAbilityEventCount = 0;

	// CDO switch: commit AbilityCost in BeginAbilityEvent; staged for the cost spec (next task); unused here.
	UPROPERTY() bool bCommitCostOnBegin = false;

	virtual void BeginAbilityEvent_Implementation() override;
	virtual void EndAbilityEvent_Implementation() override;
	virtual void CancelAbilityEvent_Implementation() override;
};
