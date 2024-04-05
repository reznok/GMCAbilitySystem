#pragma once
#include "GameplayTagContainer.h"
#include "GMCAttributeClamp.h"
#include "Effects/GMCAbilityEffect.h"
#include "GMCAttributes.generated.h"

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FAttribute
{
	GENERATED_BODY()
	FAttribute(){};

	void Init() const
	{
		CalculateValue(false);
	}

	UPROPERTY()
	mutable float AdditiveModifier{0};
	
	UPROPERTY()
	mutable float MultiplyModifier{1};

	UPROPERTY()
	mutable float DivisionModifier{1};

	void ApplyModifier(const FGMCAttributeModifier& Modifier, bool bModifyBaseValue) const
	{
		switch(Modifier.ModifierType)
		{
		case EModifierType::Add:
			if (bModifyBaseValue)
			{
				BaseValue += Modifier.Value;
				BaseValue = Clamp.ClampValue(BaseValue);
			}
			else
			{
				AdditiveModifier += Modifier.Value;
			}
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

	void CalculateValue(bool bClamp = true) const
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
		
		Value =((BaseValue + AdditiveModifier) * LocalMultiplyModifier) / LocalDivisionModifier;
		if (bClamp)
		{
			Value = Clamp.ClampValue(Value);
		}
	}

	// Reset the modifiers to the base value. May cause jank if there's effects going on.
	void ResetModifiers() const
	{
		MultiplyModifier = 1;
		DivisionModifier = 1;
	}

	// Allow for externally directly setting the BaseValue
	// Usually preferred to go through Effects/Modifiers instead of this
	void SetBaseValue(const float Value) const
	{
		BaseValue = Clamp.ClampValue(Value);
	}
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	mutable float Value{0};

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	mutable float BaseValue{0};

	// Attribute.* 
	UPROPERTY(EditDefaultsOnly, Category="Attribute", meta = (Categories="Attribute"))
	FGameplayTag Tag{FGameplayTag::EmptyTag};

	// Whether this should be bound over GMC or not.
	// NOTE: If you don't bind it, you can't use it for any kind of prediction.
	UPROPERTY(EditDefaultsOnly)
	bool bIsGMCBound = false;

	// Clamp the attribute to a certain range
	// Clamping will only happen if this is modified
	UPROPERTY(EditDefaultsOnly)
	FAttributeClamp Clamp{};

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

