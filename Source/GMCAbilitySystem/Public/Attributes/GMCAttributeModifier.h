#pragma once
#include "GameplayTags.h"
#include "GMCAttributeModifier.generated.h"

class UGMC_AbilitySystemComponent;

UENUM(BlueprintType)
enum class EModifierType : uint8
{
	// Adds to value
	Add,
	// Adds to value multiplier. Base Multiplier is 1. A modifier value of 1 will double the value.
	Multiply,
	// Adds to value divisor. Base Divisor is 1. A modifier value of 1 will halve the value.
	Divide     
};

USTRUCT(BlueprintType)
struct FGMCAttributeModifier
{
	GENERATED_BODY()

	// Attribute to apply modifier to
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Attribute", meta = (Categories="Attribute"))
	FGameplayTag AttributeTag;

	// Value to modify the attribute by
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	float Value{0};

	// Instead of hard-coding a value, you can use another attribute to get the value from
	// If set, this will take priority over Value
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="GMCAbilitySystem", meta = (Categories="Attribute"))
	FGameplayTag ValueAttributeTag;

	// Type of modifier to apply
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	EModifierType ModifierType{EModifierType::Add};

	// Metadata tags to be passed with the attribute
	// Ie: DamageType (Element.Fire, Element.Electric), DamageSource (Source.Player, Source.Boss), etc
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer MetaTags;

	float GetValue(UGMC_AbilitySystemComponent* AbilityComponent) const;
};