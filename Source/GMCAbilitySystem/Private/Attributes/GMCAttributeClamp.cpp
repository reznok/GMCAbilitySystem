#include "Attributes/GMCAttributeClamp.h"

#include "GMCAbilityComponent.h"

bool FAttributeClamp::IsSet() const
{
	return bClampMin || bClampMax;
}

float FAttributeClamp::ResolveMin() const
{
	// MinAttributeTag takes priority over the literal Min when an AbilityComponent is available
	// to resolve it.
	if (AbilityComponent && MinAttributeTag.IsValid())
	{
		return AbilityComponent->GetAttributeValueByTag(MinAttributeTag);
	}
	return Min;
}

float FAttributeClamp::ResolveMax() const
{
	if (AbilityComponent && MaxAttributeTag.IsValid())
	{
		return AbilityComponent->GetAttributeValueByTag(MaxAttributeTag);
	}
	return Max;
}

float FAttributeClamp::ClampValue(float Value) const
{
	// Neither bound is active — return Value untouched.
	if (!bClampMin && !bClampMax) { return Value; }

	float Result = Value;
	if (bClampMin) { Result = FMath::Max(Result, ResolveMin()); }
	if (bClampMax) { Result = FMath::Min(Result, ResolveMax()); }
	return Result;
}
