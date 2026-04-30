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

// How a temporal modifier participates in CalculateValue. Stored alongside the entry so the value pipeline
// can layer Sets and Adds in the right order without re-querying the source modifier's Op every frame.
UENUM()
enum class EAttributeModifierKind : uint8
{
	// Standard cumulative delta. Stacks with all other Adds. Compatible with the original BaseValue + Σdeltas model.
	Add,
	// Absolute value override. Wins over RawValue as the base layer. Active Add modifiers (any timestamp) stack on top.
	Set,
	// Absolute value override. Wins over RawValue as the base layer. Active Adds placed BEFORE this entry's
	// ActionTimer are filtered out — only Adds placed afterwards stack on top. Used for "reset state" semantics.
	SetReplace,
};

USTRUCT(Blueprintable)
struct FAttributeTemporaryModifier
{
	GENERATED_BODY()

	UPROPERTY()
	// Index used to identify the application of this modifier
	int ApplicationIndex = 0;

	UPROPERTY()
	// The value that we would like to apply (delta for Add, absolute target for Set / SetReplace)
	float Value = 0.f;

	UPROPERTY()
	double ActionTimer = 0.0;

	// The effect that applied this modifier
	UPROPERTY()
	TWeakObjectPtr<UGMCAbilityEffect> InstigatorEffect = nullptr;

	UPROPERTY()
	EAttributeModifierKind Kind = EAttributeModifierKind::Add;
};

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FAttribute : public FFastArraySerializerItem
{
	GENERATED_BODY()
	FAttribute(){};

	void Init() const
	{
		// When bStartFull is set, override InitialValue with the resolved upper clamp. ClampValue
		// applied to TNumericLimits<float>::Max() returns the literal Clamp.Max or the value of
		// Clamp.MaxAttributeTag, whichever applies. Two-pass init in UGMC_AbilitySystemComponent
		// ensures MaxAttributeTag dependencies resolve correctly even if the source attribute is
		// declared after this one.
		if (bStartFull && Clamp.IsSet())
		{
			InitialValue = Clamp.ClampValue(TNumericLimits<float>::Max());
		}
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

	int32 BoundIndex = INDEX_NONE;

	// Temporal Modifier + Accumulated Value
	// This is replicated on Simulated proxy 
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

	// Runtime mirror of FAttributeData::bStartFull. When set, Init() resolves InitialValue from
	// the upper clamp instead of using the user-set value.
	UPROPERTY()
	bool bStartFull = false;

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
	// Replicated to Simulated Proxy
	UPROPERTY()
	mutable float RawValue = 0.f;

protected:

		// Local cache of temporal contributions to Value. Deliberately NOT replicated:
		//  - For bound attributes (GMC-driven), each side rebuilds the list from its own Effect ticks during
		//    PredictionTick / replay, and PurgeTemporalModifier(ActionTimer) cleans up rolled-back entries.
		//  - For unbound attributes, the client never mutates this list (no prediction → bIsDirty stays false),
		//    so its content on the client side is always empty. The server's Value is the only thing the client
		//    consumes, and that arrives via the replicated UPROPERTY Value field on FAttribute itself.
		// Replicating this array used to send up to 25 bytes per active modifier per delta update on every
		// FAttribute change in UnBoundAttributes — pure waste for state nobody on the client reads.
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