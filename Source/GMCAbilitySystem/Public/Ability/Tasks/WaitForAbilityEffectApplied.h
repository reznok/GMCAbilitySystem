#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GMCAbilityTaskBase.h"
#include "WaitForAbilityEffectApplied.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWaitAbilityEffectAppliedDelegate, AActor*, Source, int32,Handle );


/**
 * Wait for a change in active gameplay tags matching a provided filter.
 */


class UGMC_AbilitySystemComponent;

UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_WaitForAbilityEffectApplied: public UGMCAbilityTaskBase
{
	GENERATED_UCLASS_BODY()

	UFUNCTION()
	void OnApplyAbilityEffectCallback(AActor* Target, int32 ActiveHandle);


public:
	UPROPERTY(BlueprintAssignable)
	FWaitAbilityEffectAppliedDelegate	OnApplied;


	
	virtual void Activate() override;

	
	
	FGameplayTagRequirements TagRequirements;

	bool TriggerOnce;
	bool ListenForPeriodicEffects;

	void SetExternalActor(AActor* InActor);

	bool UseExternalOwner = false;

		UPROPERTY()
	TObjectPtr<UGMC_AbilitySystemComponent> ExternalOwner;

	
	UFUNCTION(BlueprintCallable, Category="Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UGMCAbilityTask_WaitForAbilityEffectApplied* WaitForAbilityEffectApplied(UGMCAbility* OwningAbility,  FGameplayTagRequirements InTagRequirements, bool InTriggerOnce, bool
		InListenForPeriodicEffect, AActor* OptionalExternalOwner);
	


	UFUNCTION()
	void OnAbilityEffectApplied(AActor* Source, int EffectHandle);

	int32 Handle;
protected:
	virtual void OnDestroy(bool bInOwnerFinished) override;
	virtual void RegisterDelegate();
	virtual void RemoveDelegate() { }
	bool Registered;
	FDelegateHandle AppliedDelegate;
	FDelegateHandle OnAbilityEffectAppliedDelegateHandle;

	FDelegateHandle OnApplyAbilityEffectCallbackDelegateHandle;
	FDelegateHandle OnPeriodicAbilityEffectExecuteCallbackDelegateHandle;
};
