#pragma once
#include "GameplayTagContainer.h"
#include "GMCAttributeClamp.generated.h"


class UGMC_AbilitySystemComponent;

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FAttributeClamp
{
	GENERATED_BODY()

	// Whether the value is clamped at the lower bound. When false, Min and
	// MinAttributeTag are ignored entirely — there is no floor.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GMCAbilitySystem")
	bool bClampMin { true };

	// Minimum attribute value
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GMCAbilitySystem",
		meta = (EditCondition = "bClampMin"))
	float Min { 0.f };

	// Value will be clamped to the value of this attribute
	// If set, this will take priority over Min
	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem",
		meta = (Categories = "Attribute", EditCondition = "bClampMin"))
	FGameplayTag MinAttributeTag { FGameplayTag::EmptyTag };

	// Whether the value is clamped at the upper bound. When false, Max and
	// MaxAttributeTag are ignored entirely — there is no ceiling.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GMCAbilitySystem")
	bool bClampMax { true };

	// Maximum attribute value
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GMCAbilitySystem",
		meta = (EditCondition = "bClampMax"))
	float Max { 0.f };

	// Value will be clamped to the value of this attribute
	// If set, this will take priority over Max
	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem",
		meta = (Categories = "Attribute", EditCondition = "bClampMax"))
	FGameplayTag MaxAttributeTag { FGameplayTag::EmptyTag };

	UPROPERTY()
	UGMC_AbilitySystemComponent* AbilityComponent { nullptr };

	bool operator==(const FAttributeClamp* Other) const {return *this == *Other;}
	bool operator==(const FAttributeClamp& Other) const
	{
		return Other.Min == Min
			&& Other.Max == Max
			&& Other.MinAttributeTag == MinAttributeTag
			&& Other.MaxAttributeTag == MaxAttributeTag
			&& Other.bClampMin == bClampMin
			&& Other.bClampMax == bClampMax;
	}

	bool IsSet() const;
	
	float ClampValue(float Value) const;
};