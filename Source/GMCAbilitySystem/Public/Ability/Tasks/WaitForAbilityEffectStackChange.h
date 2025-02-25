#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GMCAbilityTaskBase.h"
#include "WaitForAbilityEffectStackChange.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FWaitAbilityEffectStackChangeDelegate, int32, Handle, int32, NewCount, int32, OldCount);


/**
 * Wait for a change in active gameplay tags matching a provided filter.
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_WaitForAbilityEffectStackChange: public UGMCAbilityTaskBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FWaitAbilityEffectStackChangeDelegate	OnChange;

	UPROPERTY(BlueprintAssignable)
	FWaitAbilityEffectStackChangeDelegate	InvalidHandle;
	
	virtual void Activate() override;


	UFUNCTION(BlueprintCallable, Category="Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UGMCAbilityTask_WaitForAbilityEffectStackChange* WaitForAbilityEffectStackChange(UGMCAbility* OwningAbility,int handle);
	


	UFUNCTION()
	void OnAbilityEffectStackChange(int32 Handle, int32 NewCount, int32 OldCount);

	int32 Handle;
protected:
	virtual void OnDestroy(bool bInOwnerFinished) override;
	bool Registered;
	FDelegateHandle ChangeDelegate;
	FDelegateHandle OnAbilityEffectStackChangeDelegateHandle;
};
