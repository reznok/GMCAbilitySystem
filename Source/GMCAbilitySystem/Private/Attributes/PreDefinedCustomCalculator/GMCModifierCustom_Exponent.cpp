// Fill out your copyright notice in the Description page of Project Settings.


#include "GMCModifierCustom_Exponent.h"

#include "GMCAttributes.h"

float UGMCModifierCustom_Exponent::Calculate(UGMCAbilityEffect* SourceEffect, const FAttribute* Attribute)
{

	if (!CheckValidity(SourceEffect, Attribute))	
	{
		return 0.f; // Invalid state, return 0
	}
	
	switch (ExponentType) {
		case EGMCMC_ExponentType::Mapping:
			{
				const float x = Attribute->Value * 3;
				const float rawExp = FMath::Exp(x);
				constexpr float minExp = 1.f;
				constexpr float MaxExp = 0x1.42096ff2afc4p+4; // exp(3)
		
				return Min + ((rawExp - minExp) / (MaxExp - minExp)) * (Max - Min);
				break;
			}
		case EGMCMC_ExponentType::Easing:
			{
				const float easedT = Attribute->Value == 0 ? 0 : FMath::Pow(2, 10 *(Attribute->Value - 1.f));
				return Min + easedT * (Max - Min);
				break;
			}
		case EGMCMC_ExponentType::CustomPower:
			{
				const float poweredT = FMath::Pow(Attribute->Value, k);
				return Min + poweredT * (Max - Min);
				break;
			}
		case EGMCMC_ExponentType::Saturated:
			{
				const float expValue = 1 - FMath::Exp(-k * Attribute->Value);
				return Min + expValue * (Max - Min);
				break;
			}
	}

	return Attribute->Value; // Fallback, should not happen
}
