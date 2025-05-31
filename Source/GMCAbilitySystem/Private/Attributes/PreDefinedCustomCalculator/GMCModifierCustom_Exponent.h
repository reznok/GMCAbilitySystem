// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GMCAttributeModifierCustom_Base.h"
#include "GMCModifierCustom_Exponent.generated.h"

UENUM()
enum class EGMCMC_ExponentType
{
	Mapping UMETA(DisplayName = "Mapping", ToolTip="Classique Exponent scale"),
	Easing UMETA(DisplayName = "Easing", ToolTip="Easing scale"),
	CustomPower UMETA(DisplayName = "Custom Power", ToolTip="Custom Power"),
	Saturated UMETA(DisplayName = "Saturated", ToolTip="Saturated scale")
};

/**
 * 
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMCModifierCustom_Exponent : public UGMCAttributeModifierCustom_Base
{
	GENERATED_BODY()

	public:

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = Exponent)
	EGMCMC_ExponentType ExponentType;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = Exponent)
	float Min = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = Exponent)
	float Max = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = Exponent, meta=(EditCondition = "ExponentType == EGMCMC_ExponentType::Saturated || ExponentType == EGMCMC_ExponentType::CustomPower", EditConditionHides));
	float k = 0.f;

	virtual float Calculate(UGMCAbilityEffect* SourceEffect, const FAttribute* Attribute) override;
};
