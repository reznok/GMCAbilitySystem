#include "Attributes/GMCAttributeClamp.h"

#include "GMCAbilityComponent.h"

bool FAttributeClamp::IsSet() const
{
	return bClampMin || bClampMax;
}

float FAttributeClamp::ClampValue(float Value) const
{
	// Neither bound is active — return Value untouched.
	if (!bClampMin && !bClampMax) { return Value; }

	float Result = Value;

	if (bClampMin)
	{
		// MinAttributeTag takes priority over the literal Min when an
		// AbilityComponent is available to resolve it.
		float MinBound = Min;
		if (AbilityComponent && MinAttributeTag.IsValid())
		{
			MinBound = AbilityComponent->GetAttributeValueByTag(MinAttributeTag);
		}
		Result = FMath::Max(Result, MinBound);
	}

	if (bClampMax)
	{
		float MaxBound = Max;
		if (AbilityComponent && MaxAttributeTag.IsValid())
		{
			MaxBound = AbilityComponent->GetAttributeValueByTag(MaxAttributeTag);
		}
		Result = FMath::Min(Result, MaxBound);
	}

	return Result;
}
