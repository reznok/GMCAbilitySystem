#include "Attributes/GMCAttributes.h"

#include "GMCAbilityComponent.h"

void FAttribute::AddModifier(const FGMCAttributeModifier& PendingModifier) const
{
	const float ModifierValue = PendingModifier.CalculateModifierValue(*this);

	// Map the modifier op to the temporal-entry kind. Set and SetReplace are absolute overrides; everything else
	// is treated as an additive contribution at the temporal layer.
	const bool bIsSet        = PendingModifier.Op == EModifierType::Set;
	const bool bIsSetReplace = PendingModifier.Op == EModifierType::SetReplace;
	const EAttributeModifierKind Kind = bIsSet        ? EAttributeModifierKind::Set
	                                  : bIsSetReplace ? EAttributeModifierKind::SetReplace
	                                                  : EAttributeModifierKind::Add;

	if (PendingModifier.bRegisterInHistory)
	{
		FAttributeTemporaryModifier Entry(PendingModifier.ApplicationIndex, ModifierValue, PendingModifier.ActionTimer, PendingModifier.SourceAbilityEffect);
		Entry.Kind = Kind;
		ValueTemporalModifiers.Add(Entry);
	}
	else
	{
		// Permanent path: Set/SetReplace overwrite RawValue absolutely; Add accumulates.
		// Permanent Sets are NOT replay-safe by construction (same caveat as permanent Adds today).
		if (bIsSet || bIsSetReplace)
		{
			RawValue = Clamp.ClampValue(ModifierValue);
		}
		else
		{
			RawValue = Clamp.ClampValue(RawValue + ModifierValue);
		}
	}

	bIsDirty = true;
}

void FAttribute::CalculateValue() const
{
	// Pass 1: find the winning Set/SetReplace entry (most recent ActionTimer; tie-break by ApplicationIndex).
	// Walking the entire array once instead of two separate loops keeps the cost at O(n) vs. O(2n).
	const FAttributeTemporaryModifier* WinningSet = nullptr;
	for (const FAttributeTemporaryModifier& Mod : ValueTemporalModifiers)
	{
		if (Mod.Kind == EAttributeModifierKind::Add) continue;
		if (!Mod.InstigatorEffect.IsValid())
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Orphelin Set Modifier found in FAttribute::CalculateValue"));
			checkNoEntry();
			continue;
		}
		if (!WinningSet
			|| Mod.ActionTimer > WinningSet->ActionTimer
			|| (Mod.ActionTimer == WinningSet->ActionTimer && Mod.ApplicationIndex > WinningSet->ApplicationIndex))
		{
			WinningSet = &Mod;
		}
	}

	// Pass 2: base layer. A winning Set hides RawValue entirely; otherwise we start from RawValue.
	Value = WinningSet ? Clamp.ClampValue(WinningSet->Value)
	                   : Clamp.ClampValue(RawValue);

	// Pass 3: stack the active Add modifiers. SetReplace mode filters out Adds older than the Set's ActionTimer
	// — the rationale being "the Set wiped the slate clean, only modifiers placed after the Set survive".
	const bool   bReplaceMode = WinningSet && WinningSet->Kind == EAttributeModifierKind::SetReplace;
	const double SetTime      = WinningSet ? WinningSet->ActionTimer : -DBL_MAX;

	for (const FAttributeTemporaryModifier& Mod : ValueTemporalModifiers)
	{
		if (Mod.Kind != EAttributeModifierKind::Add) continue;
		if (!Mod.InstigatorEffect.IsValid())
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Orphelin Attribute Modifier found in FAttribute::CalculateValue"));
			checkNoEntry();
			continue;
		}
		if (bReplaceMode && Mod.ActionTimer < SetTime) continue;
		Value += Mod.Value;
		Value = Clamp.ClampValue(Value);
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
