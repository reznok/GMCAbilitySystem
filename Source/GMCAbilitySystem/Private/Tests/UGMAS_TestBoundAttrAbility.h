// Ability for Layer 3 / Layer 4 GMC-bound attribute prediction tests.
//
// BeginAbility applies an instant +StaminaMod to the GMAS.Test.Attribute.Stamina
// attribute (bGMCBound=true) and immediately ends.  Because BeginAbility runs
// inside GMC's GenPredictionTick, the same code executes identically on both
// client and server → server confirms the client's predicted RawValue → no
// CL_OnClientMoveInvalidated correction fires.
//
// StaminaMod is read from the CDO so per-test overrides work the same way as
// UGMAS_TestDelayAbility::DelayTime.

#pragma once

#include "CoreMinimal.h"
#include "Ability/GMCAbility.h"
#include "UGMAS_TestBoundAttrAbility.generated.h"

UCLASS(NotBlueprintable, NotBlueprintType)
class UGMAS_TestBoundAttrAbility : public UGMCAbility
{
	GENERATED_BODY()

public:
	UGMAS_TestBoundAttrAbility()
	{
		// Must be true so QueueAbility() activates this ability inside
		// GenPredictionTick (bFromMovementTick == true).  This ensures both
		// client and server run BeginAbility in the same prediction tick,
		// making the +StaminaMod write deterministic on both sides.
		bActivateOnMovementTick = true;
	}

	// Flat add applied to Stamina in BeginAbility. Read from CDO so tests can
	// override via GetMutableDefault<UGMAS_TestBoundAttrAbility>()->StaminaMod.
	UPROPERTY(EditAnywhere, Category = "Test")
	float StaminaMod = 25.f;

	virtual void BeginAbility() override;
};
