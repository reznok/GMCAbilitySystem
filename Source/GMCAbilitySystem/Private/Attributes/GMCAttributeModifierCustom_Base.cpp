// Fill out your copyright notice in the Description page of Project Settings.


#include "GMCAttributeModifierCustom_Base.h"
#include "GMCAbilitySystem/Public/Attributes/GMCAttributes.h"

float UGMCAttributeModifierCustom_Base::Calculate(UGMCAbilityEffect* SourceEffect, const FAttribute* Attribute)
{
	if (!CheckValidity(SourceEffect, Attribute))	
	{
		return 0.f; // Invalid state, return 0
	}
			
	return K2_Calculate(SourceEffect, Attribute->Tag);
}

bool UGMCAttributeModifierCustom_Base::CheckValidity(const UGMCAbilityEffect* SourceEffect, const FAttribute* Attribute) const
{
	if (SourceEffect == nullptr)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("SourceEffect is null in UGMCAttributeModifierCustom_Base::CheckValidity"));
		return false;
	}

	if (Attribute == nullptr)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("Attribute is null in UGMCAttributeModifierCustom_Base::CheckValidity"));
		return false;
	}

	return true;
}
