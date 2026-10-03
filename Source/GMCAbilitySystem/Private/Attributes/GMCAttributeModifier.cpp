#include "Attributes/GMCAttributeModifier.h"

#include "GMCAbilityComponent.h"

float FGMCAttributeModifier::GetValue() const
{
	// Get The Value Type
	switch (ValueType)
	{
	case EGMCAttributeModifierType::AMT_Value:
		return ModifierValue;
	case EGMCAttributeModifierType::AMT_Attribute:
		{
			if (SourceAbilityEffect.IsValid() && SourceAbilityEffect->GetOwnerAbilityComponent())
			{
				return SourceAbilityEffect->GetOwnerAbilityComponent()->GetAttributeValueByTag(ValueAsAttribute);
			}
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("FGMCAttributeModifier::GetValue: AMT_Attribute modifier has no live source effect; contributes 0."));
			return 0.f;
		}
	case EGMCAttributeModifierType::AMT_Custom:
		{
			if (!CustomModifierClass)
			{
				UE_LOG(LogGMCAbilitySystem, Error, TEXT("FGMCAttributeModifier::GetValue: AMT_Custom modifier has no CustomModifierClass; contributes 0."));
				return 0.f;
			}
			if (!SourceAbilityEffect.IsValid() || !SourceAbilityEffect->GetOwnerAbilityComponent())
			{
				UE_LOG(LogGMCAbilitySystem, Error, TEXT("FGMCAttributeModifier::GetValue: AMT_Custom modifier (%s) has no live source effect; contributes 0."), *CustomModifierClass->GetName());
				return 0.f;
			}
			UGMCAttributeModifierCustom_Base* Calculator = CustomModifierClass->GetDefaultObject<UGMCAttributeModifierCustom_Base>();
			if (!Calculator)
			{
				UE_LOG(LogGMCAbilitySystem, Error, TEXT("FGMCAttributeModifier::GetValue: no default object for %s; contributes 0."), *CustomModifierClass->GetName());
				return 0.f;
			}
			// Calculators run on the class default object, shared by every modifier that names the
			// class, so Calculate must not keep per-call state on the object.
			return Calculator->Calculate(SourceAbilityEffect.Get(), SourceAbilityEffect->GetOwnerAbilityComponent()->GetAttributeByTag(AttributeTag));
		}
	case EGMCAttributeModifierType::AMT_External:
		if (SourceAbilityEffect.IsValid() && SourceAbilityEffect->GetOwnerAbilityComponent())
		{
			return SourceAbilityEffect->GetOwnerAbilityComponent()->GetExternalModifierValue(ExternalTag, ExternalValueIndex);
		}
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("FGMCAttributeModifier::GetValue: AMT_External modifier has no live source effect; contributes 0."));
		return 0.f;
	}

	UE_LOG(LogGMCAbilitySystem, Error, TEXT("FGMCAttributeModifier::GetValue: unknown ValueType %d; contributes 0."), static_cast<int32>(ValueType));
	return 0.f;
}

// One Warning per (attribute instance, op): a percentage of an unset clamp bound is a data error
// that would otherwise log every tick. The latch is FAttribute::WarnedClampOps, so it dies with
// the attribute.
static void WarnClampBoundOff(const FAttribute& Attribute, uint8 Bit, const TCHAR* OpName)
{
	if (Attribute.WarnedClampOps & Bit)
	{
		return;
	}
	Attribute.WarnedClampOps |= Bit;
	UE_LOG(LogGMCAbilitySystem, Warning, TEXT("FGMCAttributeModifier::CalculateModifierValue: %s on %s whose clamp bound is off; contributes 0."), OpName, *Attribute.Tag.ToString());
}

float FGMCAttributeModifier::CalculateModifierValue(const FAttribute& Attribute) const
{
	float TargetValue = GetValue();

	// Resolve the source ASC ONCE with the weak-ptr validated. SourceAbilityEffect is a
	// TWeakObjectPtr: a modifier kept in the attribute's temporal history can outlive its
	// effect (GC after EndEffect), and every attribute-driven op below used to dereference
	// the stale pointer raw — GetValue() above guards, these branches did not.
	UGMC_AbilitySystemComponent* SourceASC =
		SourceAbilityEffect.IsValid() ? SourceAbilityEffect->GetOwnerAbilityComponent() : nullptr;
	const auto GetSourceAttributeValue = [SourceASC](const FGameplayTag& InAttributeTag) -> float
	{
		if (SourceASC)
		{
			return SourceASC->GetAttributeValueByTag(InAttributeTag);
		}
		UE_LOG(LogGMCAbilitySystem, Error,
			TEXT("FGMCAttributeModifier::CalculateModifierValue: SourceAbilityEffect/ASC stale, attribute-driven modifier falls back to 0."));
		return 0.f;
	};

	// First set Percentage values to a fraction
	switch (Op)
	{
		case EModifierType::AddPercentageAttribute:
		case EModifierType::AddPercentageInitialValue:
		case EModifierType::AddPercentageAttributeSum:
		case EModifierType::AddPercentageMissing:
		case EModifierType::AddPercentageMinClamp:
		case EModifierType::AddPercentageMaxClamp:
		case EModifierType::AddPercentageOfAttributeRawValue:
		case EModifierType::AddPercentageOfBase:
			TargetValue /= 100.f;
		break;
	}

	switch (Op)
	{
		case EModifierType::Add:
			return TargetValue * DeltaTime;
		case EModifierType::AddPercentageInitialValue:
			return Attribute.InitialValue * TargetValue * DeltaTime;
		case EModifierType::AddPercentageAttribute:
			return GetSourceAttributeValue(ValueAsAttribute) * TargetValue * DeltaTime;
		case EModifierType::AddPercentageMaxClamp:
			{
				if (!Attribute.Clamp.bClampMax)
				{
					WarnClampBoundOff(Attribute, 1, TEXT("AddPercentageMaxClamp"));
					return 0.f;
				}
				// The bound in effect, resolved through the attribute's own component: SourceASC can
				// be stale, and falling back to the literal would silently read 0.
				return Attribute.Clamp.ResolveMax() * TargetValue * DeltaTime;
			}
		case EModifierType::AddPercentageMinClamp:
			{
				if (!Attribute.Clamp.bClampMin)
				{
					WarnClampBoundOff(Attribute, 2, TEXT("AddPercentageMinClamp"));
					return 0.f;
				}
				return Attribute.Clamp.ResolveMin() * TargetValue * DeltaTime;
			}
		case EModifierType::AddPercentageAttributeSum:
			{
				float Sum = 0.f;
				for (auto& AttTag : Attributes)
				{
					Sum += GetSourceAttributeValue(AttTag);
				}
				return TargetValue * Sum * DeltaTime;
			}
		case EModifierType::AddScaledBetween:
			{
				const float XBound = XAsAttribute ? GetSourceAttributeValue(XAttribute) : X;
				const float YBound = YAsAttribute ? GetSourceAttributeValue(YAttribute) : Y;
				// The alpha is clamped, so the result stays between the resolved bounds even when they
				// come from attributes (the literal X/Y are 0 then).
				return FMath::Lerp(XBound, YBound, FMath::Clamp(TargetValue, 0.f, 1.f)) * DeltaTime;
			}
		case EModifierType::AddClampedBetween:
			{
				const float XBound = XAsAttribute ? GetSourceAttributeValue(XAttribute) : X;
				const float YBound = YAsAttribute ? GetSourceAttributeValue(YAttribute) : Y;
				return FMath::Clamp(TargetValue, XBound, YBound) * DeltaTime;
			}
		case EModifierType::AddPercentageMissing:
			{
				const float MissingValue =  Attribute.InitialValue - Attribute.Value;
				return TargetValue * MissingValue * DeltaTime;
			}
		case EModifierType::AddPercentageOfAttributeRawValue:
			{
				// ValueAsAttribute names the source when set (what the editor shows for this op);
				// otherwise the target attribute's own RawValue. Never Value: the op is defined on
				// the raw layer.
				const FGameplayTag SourceTag = ValueAsAttribute.IsValid() ? ValueAsAttribute : Attribute.Tag;
				if (SourceTag == Attribute.Tag)
				{
					return TargetValue * Attribute.RawValue * DeltaTime;
				}
				if (!SourceASC)
				{
					UE_LOG(LogGMCAbilitySystem, Error, TEXT("FGMCAttributeModifier::CalculateModifierValue: AddPercentageOfAttributeRawValue on %s has no live source effect to read %s; contributes 0."), *Attribute.Tag.ToString(), *SourceTag.ToString());
					return 0.f;
				}
				return TargetValue * SourceASC->GetAttributeRawValue(SourceTag) * DeltaTime;
			}
		case EModifierType::Set:
		case EModifierType::SetReplace:
			// Absolute value, no DeltaTime scaling. The two Set variants share the same payload (a target value);
			// they differ only in how FAttribute::CalculateValue treats the surrounding Add modifiers.
			return TargetValue;
		case EModifierType::AddPercentageOfBase:
			// Returns the FRACTION only. FAttribute::CalculateValue multiplies it by the resolved
			// base layer at calc time (deferred: the base is not knowable here without a stale read).
			return TargetValue * DeltaTime;
	}

	UE_LOG(LogGMCAbilitySystem, Error, TEXT("FGMCAttributeModifier::CalculateModifierValue: unknown Op %d for attribute %s; contributes 0."), static_cast<int32>(Op), *Attribute.Tag.ToString());
	return 0.f;
}

bool FGMCAttributeModifier::ResolveConditions(const UGMC_AbilitySystemComponent* ASC)
{
	if (Conditions.Num() == 0 || ASC == nullptr) return true;

	// Bound-only view: ActiveTags is GMC-bound (rollback + replayed), so a condition evaluated
	// here resolves identically on every replayed move. ClientAuthActiveTags is intentionally
	// excluded — it isn't bound and would make the application non-deterministic under replay.
	const FGameplayTagContainer& BoundTags = ASC->GetBoundActiveTags();
	for (const FGMCModifierCondition& Rule : Conditions)
	{
		if (Rule.Condition.IsEmpty() || !Rule.Condition.Matches(BoundTags)) continue;

		switch (Rule.Action)
		{
		case EGMCModifierConditionAction::Skip:
			return false; // first match wins -> abort this application

		case EGMCModifierConditionAction::OverrideValue:
			ValueType           = Rule.ValueType;
			ModifierValue       = Rule.ModifierValue;
			ValueAsAttribute    = Rule.ValueAsAttribute;
			CustomModifierClass = Rule.CustomModifierClass;
			ExternalTag         = Rule.ExternalTag;
			ExternalValueIndex  = Rule.ExternalValueIndex;
			return true; // first match wins -> apply with the overridden value source
		}
	}
	return true; // no rule matched -> apply with the default value source
}

void FGMCAttributeModifier::InitModifier(UGMCAbilityEffect* Effect, double InActionTimer, int InApplicationIdx, bool bInRegisterInHistory, float InDeltaTime)
{
	if (!Effect)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("Effect or AbilitySystemComponent is null in FGMCAttributeModifier::InitModifier"));
		return;
	}

	SourceAbilityEffect = Effect;
	bRegisterInHistory = bInRegisterInHistory;
	DeltaTime = InDeltaTime;
	ApplicationIndex = InApplicationIdx;
	ActionTimer = InActionTimer;
	
}
