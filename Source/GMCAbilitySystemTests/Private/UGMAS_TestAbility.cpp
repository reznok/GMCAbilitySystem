#include "UGMAS_TestAbility.h"

void UGMAS_TestAbility::BeginAbilityEvent_Implementation()
{
	BeginAbilityEventCount++;
	if (GetDefault<UGMAS_TestAbility>()->bCommitCostOnBegin)
	{
		CommitAbilityCost();
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
