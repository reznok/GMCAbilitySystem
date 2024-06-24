#pragma once
#include "GameplayTagContainer.h"
#include "GMCAttributeClamp.h"
#include "Effects/GMCAbilityEffect.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GMCAttributes.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAttributeChanged, float, OldValue, float, NewValue);

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FAttribute : public FFastArraySerializerItem
{
	GENERATED_BODY()
	FAttribute(){};

	void Init() const
	{
		CalculateValue(false);
	}

	UPROPERTY(BlueprintAssignable)
	FAttributeChanged OnAttributeChanged;

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
	void SetBaseValue(const float NewValue) const
	{
		BaseValue = Clamp.ClampValue(NewValue);
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

	// Clamp the attribute to a certain range
	// Clamping will only happen if this is modified
	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem")
	FAttributeClamp Clamp{};

	FString ToString() const{
		return FString::Printf(TEXT("%s : %f (Bound: %d)"), *Tag.ToString(), Value, bIsGMCBound);
	}

	bool operator < (const FAttribute& Other) const
	{
		return Tag.ToString() < Other.Tag.ToString();
	}
};

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FGMCAttributeSet{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FAttribute> Attributes;

	void AddAttribute(const FAttribute& NewAttribute)
	{
		Attributes.Add(NewAttribute);
		Attributes.Sort();
	}

	TArray<FAttribute> GetAttributes() const
	{
		return Attributes;
	}

	void MarkAttributeDirty(const FAttribute& Attribute) {};
};

USTRUCT(BlueprintType)
struct FGMCUnboundAttributeSet : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FAttribute> Items;

	void AddAttribute(const FAttribute& NewAttribute)
	{
		Items.Add(NewAttribute);
		MarkArrayDirty();
	}

	TArray<FAttribute> GetAttributes() const
	{
		return Items;
	}

	void MarkAttributeDirty(const FAttribute& Attribute)
	{
		for (auto& Item : Items)
		{
			if (Item.Tag == Attribute.Tag)
			{
				MarkItemDirty(Item);
				return;
			}
		}
	}

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FAttribute, FGMCUnboundAttributeSet>(Items, DeltaParams,
			*this);
	}
};

template<>
struct TStructOpsTypeTraits<FGMCUnboundAttributeSet> : public TStructOpsTypeTraitsBase2<FGMCUnboundAttributeSet>
{
	enum
	{
		WithNetDeltaSerializer = true
	};
};