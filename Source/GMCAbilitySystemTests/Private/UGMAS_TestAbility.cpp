#include "UGMAS_TestAbility.h"

void UGMAS_TestAbility::BeginAbilityEvent_Implementation()
{
	BeginAbilityEventCount++;
	if (bCommitCostOnBegin)   // the instance's own copy: TryActivateAbility copies every CDO property
	{
		CommitAbilityCost();
	}
	if (bEndOnBegin)
	{
		EndAbility();
	}
}

void UGMAS_TestAbility::EndAbilityEvent_Implementation()
{
	EndAbilityEventCount++;
}

void UGMAS_TestAbility::CancelAbilityEvent_Implementation()
{
	CancelAbilityEventCount++;
}
