#pragma once
#include "GMCAbility.h"
#include "GMCAbilityTaskBase.h"
#include "LatentActions.h"
#include "Engine/CancellableAsyncAction.h"
#include "WaitDelay.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWaitDelayAsyncActionOutputPin);

UCLASS()
class UWaitDelayAsyncAction : public UGMCAbilityTaskBase
{
	GENERATED_BODY()
	
public:

	virtual void InternalTick(float DeltaTime) override;

	// Start UObject Functions
	virtual UWorld* GetWorld() const override
	{
		return ContextWorld.IsValid() ? ContextWorld.Get() : nullptr;
	}
	// End UObject Functions
 
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContext", HidePin = "Ability", DefaultToSelf = "Ability", BlueprintInternalUseOnly = "TRUE"), DisplayName="Wait Delay",Category = "GMCAbilitySystem/Tasks")
	static UWaitDelayAsyncAction* WaitForDelay(const UObject* WorldContext, UGMCAbility* Ability, float Duration);
 
	//Overriding BP async action base
	virtual void Activate() override;
	
private:
	/** The context world of this action. */
	TWeakObjectPtr<UWorld> ContextWorld = nullptr;
		
	float StartTime;
	float Duration;
	const float FaultTolerance = .1;
};