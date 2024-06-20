#include "Attributes/GMCAttributeMOdifier.h"

#include "GMCAbilityComponent.h"

float FGMCAttributeModifier::GetValue(UGMC_AbilitySystemComponent* AbilityComponent) const
{
	// TODO: handle infinite recursion
	
	// Return value from attribute if AbilityComponent and ValueAttributeTag is valid
	if (AbilityComponent && ValueAttributeTag.IsValid())
	{
		return AbilityComponent->GetAttributeValueByTag(ValueAttributeTag);
	}

	// Otherwise, fall back to the hard-coded value
	return Value;
}
