#pragma once
#include "GameplayTags.h"
#include "GMCAttributeModifier.generated.h"

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
		
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Attribute", meta = (Categories="Attribute"))
	FGameplayTag AttributeTag;

	// Value to modify the attribute by
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	float Value{0};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	EModifierType ModifierType{EModifierType::Add};

	// Metadata tags to be passed with the attribute
	// Ie: DamageType (Element.Fire, Element.Electric), DamageSource (Source.Player, Source.Boss), etc
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer MetaTags;
	
};