#pragma once
#include "GameplayTagContainer.h"
#include "GMCAttributeClamp.h"
#include "Effects/GMCAbilityEffect.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GMCAttributes.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAttributeChanged, float, OldValue, float, NewValue);

USTRUCT()
struct FModifierApplicationEntry
{
	GENERATED_BODY()
	
	TWeakObjectPtr<UGMCAbilityEffect> SourceEffect;
	
	FGameplayTag AttributeTarget;
	
	float ActionTimer {0};
	
	float Value {0};
	
	uint8 bIsBound : 1 {false};

};

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FAttribute : public FFastArraySerializerItem
{
	GENERATED_BODY()
	FAttribute(){};

	void Init() const
	{
		Value = Clamp.ClampValue(BaseValue);
	}

	
	void AddModifier(FGMCAttributeModifier PendingModifier, float DeltaTime) const;

	void ProcessPendingModifiers(TArray<FModifierApplicationEntry>& ModifierHistory) const;


	UPROPERTY(BlueprintAssignable)
	FAttributeChanged OnAttributeChanged;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	mutable float Value{0};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	mutable float BaseValue{0};

	// Attribute.* 
	UPROPERTY(EditDefaultsOnly, Category="Attribute", meta = (Categories="Attribute"))
	FGameplayTag Tag{FGameplayTag::EmptyTag};

	
	mutable TArray<FGMCAttributeModifier> PendingModifiers;

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
		MarkItemDirty(Items.Add_GetRef(NewAttribute));
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