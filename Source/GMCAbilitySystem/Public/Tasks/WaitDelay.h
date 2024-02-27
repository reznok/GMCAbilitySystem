#pragma once
#include "GMCAbilityTaskBase.h"
#include "WorldTime.h"
#include "WaitDelay.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWaitDelayDelegate);

UCLASS()
class UGMCAbilityTask_WaitDelay : public UGMCAbilityTaskBase
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(BlueprintAssignable)
	FWaitDelayDelegate OnFinish;

	float TimePassed = 0.f;

	virtual void Activate() override;
	virtual void TickTask(float DeltaTime) override;

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