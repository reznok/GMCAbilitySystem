#include "Tasks/WaitDelay.h"
#include "Components/GMCAbilityComponent.h"


UGMCAbilityTask_WaitDelay::UGMCAbilityTask_WaitDelay(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	Duration = 0.f;
	StartTime = 0.f;
}

UGMCAbilityTask_WaitDelay* UGMCAbilityTask_WaitDelay::WaitForDelay(UGMCAbility* InAbility,
	float Duration)
{
	// Create a new UWaitDelayAsyncAction, and store function arguments in it.
	UGMCAbilityTask_WaitDelay* DelayTask = NewAbilityTask<UGMCAbilityTask_WaitDelay>(InAbility);
	DelayTask->Duration = Duration;

	return DelayTask;
}

void UGMCAbilityTask_WaitDelay::Activate()
{
	const UWorld* World = GetWorld();
	StartTime = World->GetTimeSeconds();
	
	// Use a dummy timer handle as we don't need to store it for later but we don't need to look for something to clear
	FTimerHandle TimerHandle;
	World->GetTimerManager().SetTimer(TimerHandle, this, &UGMCAbilityTask_WaitDelay::OnTimeFinish, Duration, false);
}

void UGMCAbilityTask_WaitDelay::OnTimeFinish()
{
	OnFinish.Broadcast();
	EndTask();
}
