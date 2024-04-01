#pragma once
#include "GMCAbilityTaskBase.h"
#include "WaitDelay.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGMCAbilityTaskWaitDelayOutputPin);

UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_WaitDelay : public UGMCAbilityTaskBase
{
	GENERATED_UCLASS_BODY()
	
	float TimePassed = 0.f;

	virtual void Activate() override;
	virtual void Tick(float DeltaTime) override;
	
	UPROPERTY(BlueprintAssignable)
	FGMCAbilityTaskWaitDelayOutputPin Completed;

	/** Return debug string describing task */
	// virtual FString GetDebugString() const override;

	/** Wait specified time. This is functionally the same as a standard Delay node. */
	UFUNCTION(BlueprintCallable, Category="GMCAbility|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UGMCAbilityTask_WaitDelay* WaitDelay(UGMCAbility* OwningAbility, float Time);

private:

	void OnTimeFinish();

	float Time;
	float TimeStarted;
};