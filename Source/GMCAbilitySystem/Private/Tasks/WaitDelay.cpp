#include "Tasks/WaitDelay.h"
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
	UGMCAbilityTask_WaitDelay* MyObj = NewAbilityTask<UGMCAbilityTask_WaitDelay>(OwningAbility);
	MyObj->Time = Time;
	return MyObj;
}

void UGMCAbilityTask_WaitDelay::Activate()
{
	Super::Activate();

	bTickingTask = true;
	TimeStarted = AbilitySystemComponent->ActionTimer;
}

void UGMCAbilityTask_WaitDelay::TickTask(float DeltaTime)
{
	Super::TickTask(DeltaTime);
	if (TimeStarted + Time <= AbilitySystemComponent->ActionTimer)
	{
		OnTimeFinish();
	}
}

void UGMCAbilityTask_WaitDelay::OnTimeFinish()
{
	OnFinish.Broadcast();
	EndTask();
}
