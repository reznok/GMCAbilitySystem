#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GMCAbilityTaskBase.h"
#include "WaitForAbilityEffectRemoved.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWaitAbilityEffectRemovedDelegate, FAbilityEffectRemovalInfo, InAbilityEffectRemovalInfo);


/**
 * Wait for a change in active gameplay tags matching a provided filter.
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_WaitForAbilityEffectRemoved: public UGMCAbilityTaskBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FWaitAbilityEffectRemovedDelegate	OnRemoved;

	UPROPERTY(BlueprintAssignable)
	FWaitAbilityEffectRemovedDelegate	InvalidHandle;

	
	virtual void Activate() override;


	UFUNCTION(BlueprintCallable, Category="Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UGMCAbilityTask_WaitForAbilityEffectRemoved* WaitForAbilityEffectRemoved(UGMCAbility* OwningAbility,int handle);
	


	UFUNCTION()
	void OnAbilityEffectRemoved(const FAbilityEffectRemovalInfo& InAbilityEffectRemovalInfo);

	int32 Handle;
protected:
	virtual void OnDestroy(bool bInOwnerFinished) override;
	bool Registered;
	FDelegateHandle RemoveDelegate;
	FDelegateHandle OnAbilityEffectRemoveDelegateHandle;
};
