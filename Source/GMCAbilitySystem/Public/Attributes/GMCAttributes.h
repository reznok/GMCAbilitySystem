#pragma once
#include "GameplayTagContainer.h"
#include "Effects/GMCAbilityEffect.h"
#include "GMCAttributes.generated.h"

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FAttribute
{
	GENERATED_BODY()
	FAttribute(){};

	void Init() const
	{
		CalculateValue();
	}

	// Starting Value of the attribute. Modifiers use this for calculations.
	UPROPERTY()
	mutable float AdditiveModifier{0};

	UPROPERTY()
	mutable float MultiplyModifier{1};

	UPROPERTY()
	mutable float DivisionModifier{1};

	void ApplyModifier(const FGMCAttributeModifier& Modifier) const
	{
		switch(Modifier.ModifierType)
		{
		case EModifierType::Add:
			AdditiveModifier += Modifier.Value;
			break;
		case EModifierType::Multiply:
			MultiplyModifier += Modifier.Value;
			break;
		case EModifierType::Divide:
			DivisionModifier += Modifier.Value;
			break;
		default:
			break;
		}
		
		CalculateValue();
	}

	void CalculateValue() const
	{
		// Prevent divide by 0 and negative divisors
		float LocalDivisionModifier = DivisionModifier;
		if (LocalDivisionModifier <= 0){
			LocalDivisionModifier = 1;
		}

		// Prevent negative multipliers
		float LocalMultiplyModifier = MultiplyModifier;
		if (LocalMultiplyModifier < 0){
			LocalMultiplyModifier = 0;
		}
		
		Value = (AdditiveModifier + (BaseValue * LocalMultiplyModifier)) / LocalDivisionModifier;
	}

	// Reset the modifiers to the base value. May cause jank if there's effects going on.
	void ResetModifiers() const
	{
		AdditiveModifier = 0;
		MultiplyModifier = 1;
		DivisionModifier = 1;
	}
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	mutable float Value{0};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	mutable float BaseValue{0};

	// Attribute.* 
	UPROPERTY(EditDefaultsOnly, Category="Attribute", meta = (Categories="Attribute"))
	FGameplayTag Tag{FGameplayTag::EmptyTag};

	// Whether this should be bound over GMC or not.
	// NOTE: If you don't bind it, you can't use it for any kind of prediction.
	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem")
	bool bIsGMCBound = false;

	FString ToString() const{
		return FString::Printf(TEXT("%s : %f (Bound: %d)"), *Tag.ToString(), Value, bIsGMCBound);
	}
};

USTRUCT()
struct GMCABILITYSYSTEM_API FGMCAttributeSet{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FAttribute> Attributes;

	void AddAttribute(FAttribute NewAttribute) {Attributes.Add(NewAttribute);}

	TArray<FAttribute> GetAttributes() const{return Attributes;}
};

