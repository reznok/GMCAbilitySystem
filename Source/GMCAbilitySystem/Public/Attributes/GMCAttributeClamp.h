#pragma once
#include "GameplayTagContainer.h"
#include "GMCAttributeClamp.generated.h"


class UGMC_AbilitySystemComponent;

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FAttributeClamp
{
	GENERATED_BODY()

	// Minimum attribute value
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GMCAbilitySystem")
	float Min { 0.f };

	// Value will be clamped to the value of this attribute
	// If set, this will take priority over Min
	UPROPERTY(EditDefaultsOnly, meta=(Categories="Attribute"), Category = "GMCAbilitySystem")
	FGameplayTag MinAttributeTag { FGameplayTag::EmptyTag };

	// Maximum attribute value
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GMCAbilitySystem")
	float Max { 0.f };

	// Value will be clamped to the value of this attribute
	// If set, this will take priority over Max
	UPROPERTY(EditDefaultsOnly, meta=(Categories="Attribute"), Category = "GMCAbilitySystem")
	FGameplayTag MaxAttributeTag { FGameplayTag::EmptyTag };

	UPROPERTY()
	UGMC_AbilitySystemComponent* AbilityComponent { nullptr };

	bool operator==(const FAttributeClamp* Other) const {return *this == *Other;} 
	bool operator==(const FAttributeClamp& Other) const {return Other.Min == Min && Other.Max == Max && Other.MinAttributeTag == MinAttributeTag && Other.MaxAttributeTag == MaxAttributeTag;}

	bool IsSet() const;
	
	float ClampValue(float Value) const;
};