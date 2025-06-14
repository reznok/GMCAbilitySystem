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
			return SourceAbilityEffect->GetOwnerAbilityComponent()->GetAttributeValueByTag(ValueAsAttribute) * TargetValue * DeltaTime;
		case EModifierType::AddPercentageMaxClamp:
			{
				const float MaxValue = Attribute.Clamp.MaxAttributeTag.IsValid() ? SourceAbilityEffect->GetOwnerAbilityComponent()->GetAttributeValueByTag(Attribute.Clamp.MaxAttributeTag) : Attribute.Clamp.Max;
				return MaxValue * TargetValue * DeltaTime;
			}
		case EModifierType::AddPercentageMinClamp:
			{
				const float MinValue = Attribute.Clamp.MinAttributeTag.IsValid() ? SourceAbilityEffect->GetOwnerAbilityComponent()->GetAttributeValueByTag(Attribute.Clamp.MinAttributeTag) : Attribute.Clamp.Min;
				return MinValue * TargetValue * DeltaTime;
			}
		case EModifierType::AddPercentageAttributeSum:
			{
				float Sum = 0.f;
				for (auto& AttTag : Attributes)
				{
					Sum += SourceAbilityEffect->GetOwnerAbilityComponent()->GetAttributeValueByTag(AttTag);
				}
				return TargetValue * Sum * DeltaTime;
			}
		case EModifierType::AddScaledBetween:
			{
				const float XBound = XAsAttribute ? SourceAbilityEffect->GetOwnerAbilityComponent()->GetAttributeValueByTag(XAttribute) : X;
				const float YBound = YAsAttribute ? SourceAbilityEffect->GetOwnerAbilityComponent()->GetAttributeValueByTag(YAttribute) : Y;
				return FMath::Clamp(FMath::Lerp(XBound, YBound, TargetValue), X, Y) * DeltaTime;
			}
		case EModifierType::AddClampedBetween:
			{
				const float XBound = XAsAttribute ? SourceAbilityEffect->GetOwnerAbilityComponent()->GetAttributeValueByTag(XAttribute) : X;
				const float YBound = YAsAttribute ? SourceAbilityEffect->GetOwnerAbilityComponent()->GetAttributeValueByTag(YAttribute) : Y;
				return FMath::Clamp(TargetValue, XBound, YBound) * DeltaTime;
			}
		case EModifierType::AddPercentageMissing:
			{
				const float MissingValue =  Attribute.InitialValue - Attribute.Value;
				return TargetValue * MissingValue * DeltaTime;
			}
		case EModifierType::AddPercentageOfAttributeRawValue:
			{
				const float RawValue = SourceAbilityEffect->GetOwnerAbilityComponent()->GetAttributeRawValue(Attribute.Tag);
				return TargetValue * RawValue * DeltaTime;
			}
	}

	UE_LOG(LogGMCAbilitySystem, Error, TEXT("Unknown Modifier Type in FAttribute::AddModifier for Attribute %s, operator %d"), *Attribute.Tag.ToString(), static_cast<int32>(Op));
	checkNoEntry();
	return 0.f;
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
