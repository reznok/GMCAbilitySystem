// Helper ability for the task and attribute specs: BeginAbility applies an instant
// +StaminaMod to GMAS.Test.Attribute.Stamina (a bound attribute) through the inner apply
// path and ends at once. A spec overrides StaminaMod on the CDO; the instance inherits it.

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

	// Flat add applied to Stamina in BeginAbility. Tests override it via
	// GetMutableDefault<UGMAS_TestBoundAttrAbility>()->StaminaMod; each instance copies the CDO.
	UPROPERTY(EditAnywhere, Category = "Test")
	float StaminaMod = 25.f;

	virtual void BeginAbility() override;
};
