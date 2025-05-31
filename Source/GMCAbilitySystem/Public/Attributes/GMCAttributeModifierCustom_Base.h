// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GMCAbilitySystem.h"
#include "UObject/Object.h"
#include "GMCAttributeModifierCustom_Base.generated.h"

struct FAttribute;
class UGMCAbilityEffect;
class UGMC_AbilitySystemComponent;
/**
 * @class UGMCAttributeModifierCustom_Base
 *
 * @brief Defines a custom calculator for applying modifiers to attributes in a system.
 *
 * The UGMCAttributeModifierCustomCalculator class provides the capability to calculate
 * and apply custom attribute modifications based on user-defined logic. This is particularly
 * useful in situations where attributes need to be adjusted dynamically based on specific
 * conditions or custom input.
 *
 * It supports plug-and-play functionality for entities that utilize modifiable attributes,
 * allowing users to define and override custom calculation logic if required.
 *
 * This class is expected to serve as a base class or utility for systems where attribute
 * modifiers do not follow a predefined rule set and need bespoke calculations.
 *
 * Features include:
 * - Extension support for custom logic and behaviors.
 * - Compatibility with general modifier systems.
 */
UCLASS(Blueprintable)
class GMCABILITYSYSTEM_API UGMCAttributeModifierCustom_Base : public UObject
{
	GENERATED_BODY()

	public:

		UFUNCTION(BlueprintImplementableEvent)
		float K2_Calculate(UGMCAbilityEffect* SourceEffect, FGameplayTag AttributeTag) const;

		//  Designed to be overridden in C++ or Blueprint, this function will be called to calculate the final value of the attribute modifier.
		// If override in C++, don't call super to avoid calling the Blueprint event and pay the performance cost of the Blueprint call.
	virtual float Calculate(UGMCAbilityEffect* SourceEffect, const FAttribute* Attribute);

protected:
	bool CheckValidity(const UGMCAbilityEffect* SourceEffect, const FAttribute* Attribute) const;
};
