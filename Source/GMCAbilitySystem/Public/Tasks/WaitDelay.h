#pragma once
#include "GMCAbilityTaskBase.h"
#include "WaitDelay.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWaitDelayDelegate);

UCLASS()
class UGMCAbilityTask_WaitDelay : public UGMCAbilityTaskBase
{
	GENERATED_UCLASS_BODY()
	
	UPROPERTY(BlueprintAssignable)
	FWaitDelayDelegate	OnFinish;
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", BlueprintInternalUseOnly = "TRUE"), DisplayName="Wait Delay",Category = "GMCAbilitySystem/Tasks")
	static UGMCAbilityTask_WaitDelay* WaitForDelay(UGMCAbility* InAbility, float Duration);
	
	//Overriding BP async action base
	virtual void Activate() override;
	
private:
	/** The context world of this action. */
	TWeakObjectPtr<UWorld> ContextWorld = nullptr;
		
	float StartTime;
	float Duration;
	const float FaultTolerance = .1;

	void OnTimeFinish();
};