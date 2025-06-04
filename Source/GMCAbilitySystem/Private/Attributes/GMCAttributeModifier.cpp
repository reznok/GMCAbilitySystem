#include "Attributes/GMCAttributeModifier.h"

#include "GMCAbilityComponent.h"

float FGMCAttributeModifier::GetModifierValue(const FAttribute* LinkedAttribute, float DeltaTime) const
{

	if (LinkedAttribute == nullptr)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("Tried to apply a modifier without a valid LinkedAttribute for Attribute: %s"), *AttributeTag.ToString());
		return 0.f;
	}

	float TempModifierValue = ModifierValue;

	if (Op == EModifierType::Percentage
		|| Op == EModifierType::AddPercentageBaseValue
		|| Op == EModifierType::AddPercentage
		|| Op == EModifierType::PercentageBaseValue)
	{
		 TempModifierValue /= 100.f;
	}
	
	switch (ValueType)
	{
		case EGMCAttributeModifierType::AMT_Attribute:
			if (!SourceAbilitySystemComponent.IsValid())
			{
				UE_LOG(LogGMCAbilitySystem, Error, TEXT("Tried to apply an attribute modifier without a valid SourceAbilitySystemComponent for Attribute: %s"), *AttributeTag.ToString());
				return TempModifierValue;
			}
			TempModifierValue = SourceAbilitySystemComponent->GetAttributeValueByTag(AttributeTag);
			break;
		case EGMCAttributeModifierType::AMT_Custom:
			if (!SourceAbilityEffect.IsValid())
			{
				UE_LOG(LogGMCAbilitySystem, Error, TEXT("Tried to apply a custom modifier without a valid SourceAbilityEffect for Attribute: %s"), *AttributeTag.ToString());
				return TempModifierValue;
			}
			TempModifierValue = SourceAbilityEffect->ProcessCustomModifier(CustomModifierClass, LinkedAttribute);
			break;
	}
	
	switch (Op)
	{
		case EModifierType::Add:
			if (DeltaTime != -1.f) {
				TempModifierValue *= DeltaTime;
			}
			break;
	case EModifierType::Percentage:
	case EModifierType::PercentageBaseValue:
		{
			if (DeltaTime != -1.f) {
				if (TempModifierValue >= 0.0f) {
					TempModifierValue = FMath::Pow(TempModifierValue, DeltaTime);
				} else {
					// Avoid NaN
					TempModifierValue = FMath::Pow(FMath::Abs(TempModifierValue), DeltaTime);
				}
			}
		}
		break;
	case EModifierType::AddPercentage:
	case EModifierType::AddPercentageBaseValue:
		{
			if (DeltaTime != -1.f) {
				TempModifierValue = TempModifierValue * DeltaTime;
			}
		}
		break;
	}

	return TempModifierValue;
}
