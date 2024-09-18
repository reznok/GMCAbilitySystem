#include "Ability/Tasks/WaitDelay.h"
#include "Components/GMCAbilityComponent.h"
#include "Kismet/GameplayStatics.h"


UGMCAbilityTask_WaitDelay::UGMCAbilityTask_WaitDelay(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Time = 0.f;
	TimeStarted = 0.f;
}

UGMCAbilityTask_WaitDelay* UGMCAbilityTask_WaitDelay::WaitDelay(UGMCAbility* OwningAbility, float Time)
{
	UGMCAbilityTask_WaitDelay* Task = NewAbilityTask<UGMCAbilityTask_WaitDelay>(OwningAbility);
	Task->Time = Time;
	return Task;
}

void UGMCAbilityTask_WaitDelay::Activate()
{
	Super::Activate();

	bTickingTask = true;
	TimeStarted = AbilitySystemComponent->ActionTimer;
}

void UGMCAbilityTask_WaitDelay::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (TimeStarted + Time <= AbilitySystemComponent->ActionTimer)
	{
		OnTimeFinish();
	}
}

void UGMCAbilityTask_WaitDelay::OnTimeFinish()
{
	if (GetState() != EGameplayTaskState::Finished)
	{
		Completed.Broadcast();
		EndTask();
	}
}
