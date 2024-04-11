#pragma once
#include "Attributes/GMCAttributeClamp.h"

#include "GMCAbilityComponent.h"

float FAttributeClamp::ClampValue(float Value) const
{
	// Clamp not set, return Value
	if (this == FAttributeClamp{}){return Value;}

	// No AbilityComponent, clamp to Min and Max
	if (!AbilityComponent)
	{
		return FMath::Clamp(Value, Min, Max);
	}

	float AttributeMin = Min;
	float AttributeMax = Max;
	
	// Get MinAttributeTag value
	if (MinAttributeTag.IsValid())
	{
		AttributeMin = AbilityComponent->GetAttributeValueByTag(MinAttributeTag);
	}

	if (MaxAttributeTag.IsValid())
	{
		AttributeMax = AbilityComponent->GetAttributeValueByTag(MaxAttributeTag);
	}
	
	return FMath::Clamp(Value, AttributeMin, AttributeMax);
}
