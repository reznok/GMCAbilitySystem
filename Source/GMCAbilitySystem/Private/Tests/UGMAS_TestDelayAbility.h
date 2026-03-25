// Test ability that starts a WaitDelay task and ends itself when the delay fires.
// Used by Layer-3 functional tests to exercise the WaitDelay task in a live game world.

#pragma once

#include "CoreMinimal.h"
#include "Ability/GMCAbility.h"
#include "UGMAS_TestDelayAbility.generated.h"

UCLASS(NotBlueprintable, NotBlueprintType)
class UGMAS_TestDelayAbility : public UGMCAbility
{
	GENERATED_BODY()

public:
	// How long to wait before firing Completed and ending the ability.
	// Configured per-test by writing to the CDO before activation.
	UPROPERTY(EditAnywhere, Category = "Test")
	float DelayTime = 0.2f;

	virtual void BeginAbility() override;

private:
	UFUNCTION()
	void OnDelayCompleted();
};
