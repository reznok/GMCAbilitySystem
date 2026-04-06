// Test ability that starts a WaitForGameplayTagChange task and ends itself when
// the tag condition is met.  Used by Layer-3 functional tests to exercise the
// WaitForGameplayTagChange task in a live game world.

#pragma once

#include "CoreMinimal.h"
#include "Ability/GMCAbility.h"
#include "Ability/Tasks/WaitForGameplayTagChange.h"
#include "UGMAS_TestTagWatchAbility.generated.h"

UCLASS(NotBlueprintable, NotBlueprintType)
class UGMAS_TestTagWatchAbility : public UGMCAbility
{
	GENERATED_BODY()

public:
	// Tag to watch — configured per-test via the CDO before activation.
	UPROPERTY(EditAnywhere, Category = "Test")
	FGameplayTag WatchTag;

	// Whether to wait for the tag to be Set, Unset, or Changed.
	UPROPERTY(EditAnywhere, Category = "Test")
	TEnumAsByte<EGMCWaitForGameplayTagChangeType> WatchType = EGMCWaitForGameplayTagChangeType::Set;

	virtual void BeginAbility() override;

private:
	UFUNCTION()
	void OnTagChanged(FGameplayTagContainer MatchedTags);
};
