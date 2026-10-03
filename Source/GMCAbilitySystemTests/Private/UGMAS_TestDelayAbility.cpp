#include "UGMAS_TestDelayAbility.h"
#include "Ability/Tasks/WaitDelay.h"

void UGMAS_TestDelayAbility::BeginAbility()
{
	Super::BeginAbility();
	if (AbilityState == EAbilityState::Ended) return;   // cancelled inside Super: no task on an ended ability

	// The instance's own DelayTime: TryActivateAbility copies every CDO property, so a per-test
	// write to the CDO reaches it.
	const float Delay = DelayTime;

	UGMCAbilityTask_WaitDelay* Task = UGMCAbilityTask_WaitDelay::WaitDelay(this, Delay);
	Task->Completed.AddDynamic(this, &UGMAS_TestDelayAbility::OnDelayCompleted);
	Task->ReadyForActivation();
}

void UGMAS_TestDelayAbility::OnDelayCompleted()
{
	EndAbility();
}
