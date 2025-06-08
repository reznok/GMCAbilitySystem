#include "Attributes/GMCAttributes.h"

#include "GMCAbilityComponent.h"

void FAttribute::AddModifier(const FGMCAttributeModifier& PendingModifier) const
{
	
	const float ModifierValue = PendingModifier.CalculateModifierValue(*this);
	
	if (PendingModifier.bRegisterInHistory)
	{
		ValueTemporalModifiers.Add(FAttributeTemporaryModifier(PendingModifier.ApplicationIndex, ModifierValue, PendingModifier.ActionTimer, PendingModifier.SourceAbilityEffect));
	}
	else
	{
		RawValue = Clamp.ClampValue(RawValue + ModifierValue);
	}

	bIsDirty = true;
}

void FAttribute::CalculateValue() const
{

	Value = Clamp.ClampValue(RawValue);
	
	for (auto& Mod : ValueTemporalModifiers)
	{
		if (Mod.InstigatorEffect.IsValid())
		{
			Value += Mod.Value;
			Value = Clamp.ClampValue(Value);
		}
		else
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Orphelin Attribute Modifier found in FAttribute::CalculateValue"));
			checkNoEntry();
		}
	}

	bIsDirty = false;
}

void FAttribute::RemoveTemporalModifier(int ApplicationIndex, const UGMCAbilityEffect* InstigatorEffect) const
{
	for (int i = ValueTemporalModifiers.Num() - 1; i >= 0; i--)
	{
		if (ValueTemporalModifiers[i].ApplicationIndex == ApplicationIndex && ValueTemporalModifiers[i].InstigatorEffect == InstigatorEffect)
		{
			ValueTemporalModifiers.RemoveAt(i);
			bIsDirty = true;
		}
	}
}

void FAttribute::PurgeTemporalModifier(double CurrentActionTimer)
{

	if (!bIsGMCBound)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("PurgeTemporalModifier called on an unbound attribute %s"), *Tag.ToString());
		checkNoEntry();
		return;
	}
	
	bool bMustReprocessModifiers = false;
	for (int i = ValueTemporalModifiers.Num() - 1; i >= 0; i--)
	{
		if (ValueTemporalModifiers[i].ActionTimer > CurrentActionTimer)
		{
			ValueTemporalModifiers.RemoveAt(i);
			bMustReprocessModifiers = true;
		}
	}

	if (bMustReprocessModifiers)
	{
		bIsDirty = true;
	}
}

FString FAttribute::ToString() const
{
	if (bIsGMCBound)
	{
		return FString::Printf(TEXT("%s : %0.3f Bound[n%i/%0.2fmb]"), *Tag.ToString(), Value, ValueTemporalModifiers.Num(), ValueTemporalModifiers.GetAllocatedSize() / 1048576.0f);
	}
	else
	{
		return FString::Printf(TEXT("%s : %0.3f"), *Tag.ToString(), Value);
	}
}

bool FAttribute::operator<(const FAttribute& Other) const
{
	return Tag.ToString() < Other.Tag.ToString();
}
