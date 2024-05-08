#include "Attributes/GMCAttributeClamp.h"

#include "GMCAbilityComponent.h"

bool FAttributeClamp::IsSet() const
{
	return Min != 0.f || Max != 0.f || MinAttributeTag != FGameplayTag::EmptyTag || MaxAttributeTag != FGameplayTag::EmptyTag;
}

float FAttributeClamp::ClampValue(float Value) const
{
	// Clamp not set, return Value
	if (!IsSet()) {return Value;}

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
