#include "Attributes/GMCAttributes.h"

#include "GMCAbilityComponent.h"

void FAttribute::AddModifier(const FGMCAttributeModifier& PendingModifier) const
{
	const float ModifierValue = PendingModifier.CalculateModifierValue(*this);

	// Map the modifier op to the temporal-entry kind. Set and SetReplace are absolute overrides;
	// AddPercentageOfBase stores a fraction resolved at calc time; everything else is an additive delta.
	const bool bIsSet         = PendingModifier.Op == EModifierType::Set;
	const bool bIsSetReplace  = PendingModifier.Op == EModifierType::SetReplace;
	const bool bIsPctOfBase   = PendingModifier.Op == EModifierType::AddPercentageOfBase;
	const EAttributeModifierKind Kind = bIsSet        ? EAttributeModifierKind::Set
	                                  : bIsSetReplace ? EAttributeModifierKind::SetReplace
	                                  : bIsPctOfBase  ? EAttributeModifierKind::PercentOfBase
	                                                  : EAttributeModifierKind::Add;

	if (PendingModifier.bRegisterInHistory)
	{
		FAttributeTemporaryModifier Entry(PendingModifier.ApplicationIndex, ModifierValue, PendingModifier.ActionTimer, PendingModifier.SourceAbilityEffect);
		Entry.Kind = Kind;
		ValueTemporalModifiers.Add(Entry);
	}
	else
	{
		// Permanent path: Before clamping, ensure any attributes we depend on are calculated
		auto EnsureAttributeUpdated = [this](const FGameplayTag& AttributeTag)
		{
			if (AttributeTag.IsValid() && Clamp.AbilityComponent)
			{
				if (const FAttribute* Attr = Clamp.AbilityComponent->GetAttributeByTag(AttributeTag))
				{
					if (Attr->IsDirty())
					{
						Attr->CalculateValue();
						Attr->bIsDirty = true; // Preserve dirty flag for ProcessAttributes
					}
				}
			}
		};
        
		EnsureAttributeUpdated(Clamp.MinAttributeTag);
		EnsureAttributeUpdated(Clamp.MaxAttributeTag);
		
		// Permanent path: Set/SetReplace overwrite RawValue absolutely; AddPercentageOfBase scales it; Add accumulates.
		// Permanent Sets are NOT replay-safe by construction (same caveat as permanent Adds today).
		if (bIsSet || bIsSetReplace)
		{
			RawValue = Clamp.ClampValue(ModifierValue);
		}
		else if (bIsPctOfBase)
		{
			RawValue = Clamp.ClampValue(RawValue * (1.f + ModifierValue));
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
		if (Mod.Kind != EAttributeModifierKind::Set && Mod.Kind != EAttributeModifierKind::SetReplace) continue;
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

	// SetReplace mode filters out entries older than the Set's ActionTimer — the rationale being
	// "the Set wiped the slate clean, only modifiers placed after the Set survive".
	const bool   bReplaceMode = WinningSet && WinningSet->Kind == EAttributeModifierKind::SetReplace;
	const double SetTime      = WinningSet ? WinningSet->ActionTimer : -DBL_MAX;

	// Pass 3: percent-of-base entries. Each stores a FRACTION; the multiplicand is the frozen base
	// layer (never the running Value), so entries sum instead of compounding and cannot feed back.
	const float BaseLayer = Value;
	for (const FAttributeTemporaryModifier& Mod : ValueTemporalModifiers)
	{
		if (Mod.Kind != EAttributeModifierKind::PercentOfBase) continue;
		if (!Mod.InstigatorEffect.IsValid())
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Orphelin PercentOfBase Modifier found in FAttribute::CalculateValue"));
			checkNoEntry();
			continue;
		}
		if (bReplaceMode && Mod.ActionTimer < SetTime) continue;
		Value += BaseLayer * Mod.Value;
		Value = Clamp.ClampValue(Value);
	}

	// Pass 4: stack the active flat Add modifiers.
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

FString FAttribute::DumpDebugString() const
{
	FString Out = FString::Printf(TEXT("%s | Value=%0.3f RawValue=%0.3f Initial=%0.3f Bound=%d Dirty=%d Clamp=[%0.1f..%0.1f min:%d max:%d]"),
		*Tag.ToString(), Value, RawValue, InitialValue, bIsGMCBound ? 1 : 0, bIsDirty ? 1 : 0,
		Clamp.Min, Clamp.Max, Clamp.bClampMin ? 1 : 0, Clamp.bClampMax ? 1 : 0);
	Out += FString::Printf(TEXT("\n  %d temporal modifier(s):"), ValueTemporalModifiers.Num());
	for (const FAttributeTemporaryModifier& Mod : ValueTemporalModifiers)
	{
		Out += FString::Printf(TEXT("\n  - Kind=%s Value=%0.4f AppIdx=%d ActionTimer=%0.3f Effect=%s"),
			*StaticEnum<EAttributeModifierKind>()->GetNameStringByValue(static_cast<int64>(Mod.Kind)),
			Mod.Value, Mod.ApplicationIndex, Mod.ActionTimer,
			*GetNameSafe(Mod.InstigatorEffect.Get()));
	}
	return Out;
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
