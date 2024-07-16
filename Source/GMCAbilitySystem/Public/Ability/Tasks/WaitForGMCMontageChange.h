#pragma once
#include "GMCAbilityTaskBase.h"
#include "GMCOrganicMovementComponent.h"
#include "WaitForGMCMontageChange.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGMCAbilityTaskWaitForGMCMontageChangeDelayOutputPin);

UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_WaitForGMCMontageChange : public UGMCAbilityTaskBase
{
	GENERATED_UCLASS_BODY()

	virtual void Activate() override;
	virtual void Tick(float DeltaTime) override;
	
	UPROPERTY(BlueprintAssignable)
	FGMCAbilityTaskWaitForGMCMontageChangeDelayOutputPin Completed;

	/** Return debug string describing task */
	// virtual FString GetDebugString() const override;

	/** Triggers if the montage changes (Allows for Networked Interrupt). *ONLY WORKS FOR ORGANIC MOVEMENT COMPONENT* */
	UFUNCTION(BlueprintCallable, Category="GMCAbility|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UGMCAbilityTask_WaitForGMCMontageChange* WaitForGMCMontageChange(UGMCAbility* OwningAbility);

	UPROPERTY()
	UAnimMontage* StartingMontage;

	UPROPERTY()
	UGMC_OrganicMovementCmp* OrganicMovementCmp;
	
private:
	void OnFinish();
};