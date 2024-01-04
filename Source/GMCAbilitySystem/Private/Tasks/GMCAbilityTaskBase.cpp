#include "Tasks/GMCAbilityTaskBase.h"
#include "GMCAbility.h"
#include "Components/GMCAbilityComponent.h"

void UGMCAbilityTaskBase::SetAbilitySystemComponent(UGMC_AbilityComponent* InAbilitySystemComponent)
{
	this->AbilitySystemComponent = InAbilitySystemComponent;
}
