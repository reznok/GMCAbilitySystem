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

USTRUCT()
struct FModifierHistory
{
	GENERATED_BODY()

	public:

		void AddMoveHistory(UGMCAbilityEffect* InstigatorEffect, float ActionTimer, float Value, bool bIsBound);

		void CleanMoveHistory(float CurrentActionTimer);

		// Remove all entries, if bPurge is true, we will remove entries readed
		float ExtractFromMoveHistory(UGMCAbilityEffect* InstigatorEffect, bool bPurge);

		int Num() const
		{
			return History.Num() + ConcatenatedHistory.Num();
		}

		int GetAllocatedSize() const
		{
			return History.GetAllocatedSize() + ConcatenatedHistory.GetAllocatedSize();
		}

	private:
	
		// Bound Attribute who can be replayed at any moment
		TArray<FModifierHistoryEntry> History;

		// Unbound attribute, and attribute who doesn't need to be replayed (Action timer > Oldest Action Timer)
		TArray<FModifierHistoryEntry> ConcatenatedHistory;
	
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

	// Return true if the attribute has been modified
	bool ProcessPendingModifiers(float ActionTimer) const;


	UPROPERTY(BlueprintAssignable)
	FAttributeChanged OnAttributeChanged;
	
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
	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem", meta=(TitleProperty="({min}, {max} {MinAttributeTag}, {MaxAttributeTag})"))
	FAttributeClamp Clamp{};

	mutable FModifierHistory ModifierHistory;

	FString ToString() const{
		if (bIsGMCBound)
		{
			return FString::Printf(TEXT("%s : %0.3f Bound[n%i/%0.4fmb]"), *Tag.ToString(), Value, ModifierHistory.Num(), ModifierHistory.GetAllocatedSize() / 1048576.0f);
		}
		else
		{
			return FString::Printf(TEXT("%s : %0.3f"), *Tag.ToString(), Value);
		}
	}

	bool operator < (const FAttribute& Other) const
	{
		return Tag.ToString() < Other.Tag.ToString();
	}

	protected:
	
	mutable TArray<FGMCAttributeModifier> PendingModifiers;

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