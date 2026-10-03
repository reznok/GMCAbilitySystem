// Records the component's dynamic delegates so specs can assert on event order and payloads.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/GMCAbilityComponent.h"
#include "UGMAS_TestEventRecorder.generated.h"

class UGMCAbility;
class UGMCAbilityEffect;

UCLASS(NotBlueprintable, NotBlueprintType)
class UGMAS_TestEventRecorder : public UObject
{
	GENERATED_BODY()

public:
	void Bind(UGMC_AbilitySystemComponent* InComponent);

	UPROPERTY() TArray<int> AppliedIDs;
	// For each OnEffectApplied: GetEffectById(id) returned the same instance at that moment.
	UPROPERTY() TArray<bool> AppliedQueryable;
	UPROPERTY() TArray<int> RemovedIDs;
	UPROPERTY() int AbilityEndedCount = 0;
	UPROPERTY() TArray<FGameplayTag> CustomEvents;

	// When set, OnEffectApplied removes the effect it was told about (a listener ending an effect
	// mid-start).
	UPROPERTY() bool bRemoveOnApplied = false;

private:
	UFUNCTION() void OnEffectApplied(UGMCAbilityEffect* AppliedEffect);
	UFUNCTION() void OnEffectRemoved(UGMCAbilityEffect* RemovedEffect);
	UFUNCTION() void OnAbilityEnded(UGMCAbility* Ability);
	UFUNCTION() void OnCustomEvent(FGameplayTag EventTag, FInstancedStruct Payload);

	TWeakObjectPtr<UGMC_AbilitySystemComponent> Component;
};
