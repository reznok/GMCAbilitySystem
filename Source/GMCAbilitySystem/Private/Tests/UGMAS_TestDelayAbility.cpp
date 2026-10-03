#include "Tests/UGMAS_TestDelayAbility.h"
#include "Ability/Tasks/WaitDelay.h"

void UGMAS_TestDelayAbility::BeginAbility()
{
	Super::BeginAbility();

	// Read DelayTime from the CDO so per-test writes to the CDO are always
	// picked up, even if TryActivateAbility instantiates from the CDO.
	const float Delay = GetDefault<UGMAS_TestDelayAbility>()->DelayTime;

	UGMCAbilityTask_WaitDelay* Task = UGMCAbilityTask_WaitDelay::WaitDelay(this, Delay);
	Task->Completed.AddDynamic(this, &UGMAS_TestDelayAbility::OnDelayCompleted);
	Task->ReadyForActivation();
}

void UGMAS_TestDelayAbility::OnDelayCompleted()
{
	EndAbility();
}
