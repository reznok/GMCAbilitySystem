#pragma once

#include "CoreMinimal.h"
#include "GMCAbilityTaskBase.h"
#include "WaitForGameplayTagChange.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGMCAbilityTaskWaitForGameplayTagChangeAsyncActionPin, FGameplayTagContainer, MatchedTags);

UENUM()
enum EGMCWaitForGameplayTagChangeType : uint8
{
	Set UMETA(Tooltip="A matching gameplay tag must be present."),
	Unset UMETA(Tooltip="A matching gameplay tag CANNOT be present."),
	Changed UMETA(Tooltip="A matching gameplay tag must change state.")
};

/**
 * Wait for a change in active gameplay tags matching a provided filter.
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_WaitForGameplayTagChange : public UGMCAbilityTaskBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FGMCAbilityTaskWaitForGameplayTagChangeAsyncActionPin Completed;

	FGameplayTagContainer Tags;
	EGMCWaitForGameplayTagChangeType ChangeType;
	
	UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", HidePin="OwningAbility", DefaultToSelf="OwningAbility", DisplayName="Wait for Gameplay Tag Change"), Category="GMCAbilitySystem|Tasks")
	static UGMCAbilityTask_WaitForGameplayTagChange* WaitForGameplayTagChange(UGMCAbility* OwningAbility, const FGameplayTagContainer& WatchedTags, EGMCWaitForGameplayTagChangeType ChangeType = Changed);
	
	virtual void Activate() override;

	virtual void OnGameplayTagChanged(const FGameplayTagContainer& AddedTags, const FGameplayTagContainer& RemovedTags);
	
private:

	FDelegateHandle ChangeDelegate;
	
};
