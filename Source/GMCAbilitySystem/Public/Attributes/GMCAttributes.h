#pragma once
#include "GameplayTagContainer.h"
#include "GMCAttributeClamp.h"
#include "Effects/GMCAbilityEffect.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GMCAttributes.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAttributeChanged, float, OldValue, float, NewValue);



USTRUCT()
struct FModifierHistoryEntry
{
	GENERATED_BODY()

	TWeakObjectPtr<UGMCAbilityEffect> InstigatorEffect;
	
	float ActionTimer = 0.f;

	float Value = 0.f;
};

USTRUCT(Blueprintable)
struct FAttributeTemporaryModifier
{
	GENERATED_BODY()

	UPROPERTY()
	// Index used to identify the application of this modifier
	int ApplicationIndex = 0;

	UPROPERTY()
	// The value that we would like to apply
	float Value = 0.f;

	UPROPERTY()
	double ActionTimer = 0.0;

	// The effect that applied this modifier
	UPROPERTY()
	TWeakObjectPtr<UGMCAbilityEffect> InstigatorEffect = nullptr;
};

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FAttribute : public FFastArraySerializerItem
{
	GENERATED_BODY()
	FAttribute(){};

	void Init() const
	{
		RawValue = Clamp.ClampValue(InitialValue);
		CalculateValue();
	}

	
	void AddModifier(const FGMCAttributeModifier& PendingModifier) const;

	// Return true if the attribute has been modified
	void CalculateValue() const;

	void RemoveTemporalModifier(int ApplicationIndex, const UGMCAbilityEffect* InstigatorEffect) const;

	// Used to purge "future modifiers" during replay
	void PurgeTemporalModifier(double CurrentActionTimer);
	

	UPROPERTY(BlueprintAssignable)
	FAttributeChanged OnAttributeChanged;

	// Temporal Modifier + Accumulated Value
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	mutable float Value{0};

	// Value when the attribute has been initialized
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	mutable float InitialValue{0};

	// Attribute.* 
	UPROPERTY(EditDefaultsOnly, Category="Attribute", meta = (Categories="Attribute"))
	FGameplayTag Tag{FGameplayTag::EmptyTag};
	
	// Whether this should be bound over GMC or not.
	// NOTE: If you don't bind it, you can't use it for any kind of prediction.
	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem")
	bool bIsGMCBound = false;

	// Clamp the attribute to a certain range
	// Clamping will only happen if this is modified
	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem", meta=(TitleProperty="({min}, {max} {MinAttributeTag}, {MaxAttributeTag})"))
	FAttributeClamp Clamp{};

	FString ToString() const;

	bool IsDirty() const
	{
		return bIsDirty;
	}

	bool operator< (const FAttribute& Other) const;

	// This is the sum of permanent modification applied to this attribute.
	UPROPERTY()
	mutable float RawValue = 0.f;

protected:

		UPROPERTY()
		mutable TArray<FAttributeTemporaryModifier> ValueTemporalModifiers;

		mutable bool bIsDirty = false;
	
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
		return FFastArraySerializer::FastArrayDeltaSerialize<FAttribute, FGMCUnboundAttributeSet>(Items, DeltaParams,*this);
	}
};

template<>
struct TStructOpsTypeTraits<FGMCUnboundAttributeSet> : public TStructOpsTypeTraitsBase2<FGMCUnboundAttributeSet>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};