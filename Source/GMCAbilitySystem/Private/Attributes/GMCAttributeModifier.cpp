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
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("SourceAbilityEffect is null in FAttribute::AddModifier"));
			return 0.f;
		}
	case EGMCAttributeModifierType::AMT_Custom:
		if (CustomModifierClass && SourceAbilityEffect.IsValid() && SourceAbilityEffect->GetOwnerAbilityComponent())
		{
			if (UGMCAttributeModifierCustom_Base* CustomModifier = CustomModifierClass->GetDefaultObject<UGMCAttributeModifierCustom_Base>())
			{
				return CustomModifier->Calculate(SourceAbilityEffect.Get(), SourceAbilityEffect->GetOwnerAbilityComponent()->GetAttributeByTag(AttributeTag));
			}
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Custom Modifier Class is null in FAttribute::AddModifier"));
		}
		else
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("CustomModifierClass is null or SourceAbilityEffect/SourceAbilitySystemComponent is invalid in FAttribute::AddModifier"));
		}
		break;
	}

	checkNoEntry()
	return 0.f;
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
				const float MaxValue = Attribute.Clamp.MaxAttributeTag.IsValid() && SourceASC ? GetSourceAttributeValue(Attribute.Clamp.MaxAttributeTag) : Attribute.Clamp.Max;
				return MaxValue * TargetValue * DeltaTime;
			}
		case EModifierType::AddPercentageMinClamp:
			{
				const float MinValue = Attribute.Clamp.MinAttributeTag.IsValid() && SourceASC ? GetSourceAttributeValue(Attribute.Clamp.MinAttributeTag) : Attribute.Clamp.Min;
				return MinValue * TargetValue * DeltaTime;
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
				return FMath::Clamp(FMath::Lerp(XBound, YBound, TargetValue), X, Y) * DeltaTime;
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
				const float RawValue = SourceASC ? SourceASC->GetAttributeRawValue(Attribute.Tag) : Attribute.Value;
				return TargetValue * RawValue * DeltaTime;
			}
		case EModifierType::Set:
		case EModifierType::SetReplace:
			// Absolute value, no DeltaTime scaling. The two Set variants share the same payload (a target value);
			// they differ only in how FAttribute::CalculateValue treats the surrounding Add modifiers.
			return TargetValue;
	}

	UE_LOG(LogGMCAbilitySystem, Error, TEXT("Unknown Modifier Type in FAttribute::AddModifier for Attribute %s, operator %d"), *Attribute.Tag.ToString(), static_cast<int32>(Op));
	checkNoEntry();
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
