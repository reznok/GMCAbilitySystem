// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "GMCAbilitySystem.h"
#include "GMCAbilityEffect.generated.h"

class UGMC_AbilitySystemComponent;

UENUM(BlueprintType)
enum class EEffectType : uint8
{
	Instant,  // Applies Instantly
	Duration, // Lasts for X time
	Infinite  // Lasts forever
};

UENUM(BlueprintType)
enum class EEffectState : uint8
{
	Initialized,  // Applies Instantly
	Started, // Lasts for X time
	Ended  // Lasts forever
};

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
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	float Value{0};

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	EModifierType ModifierType{EModifierType::Add};

	// Metadata tags to be passed with the attribute
	// Ie: DamageType (Element.Fire, Element.Electric), DamageSource (Source.Player, Source.Boss), etc
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FGameplayTagContainer MetaTags;
	
};

// Container for exposing the attribute modifier to blueprints
UCLASS()
class GMCABILITYSYSTEM_API UGMCAttributeModifierContainer : public UObject
{
	GENERATED_BODY()
	
public:
	UPROPERTY(BlueprintReadWrite)
	FGMCAttributeModifier AttributeModifier;
};

USTRUCT(BlueprintType)
struct FGMCAbilityEffectData
{
	GENERATED_BODY()

	FGMCAbilityEffectData():SourceAbilityComponent(nullptr),
							OwnerAbilityComponent(nullptr),
							EffectID(0),
	                         StartTime(0),
	                         EndTime(0)
	{
	}

	UPROPERTY(BlueprintReadWrite)
	UGMC_AbilitySystemComponent* SourceAbilityComponent;

	UPROPERTY()
	UGMC_AbilitySystemComponent* OwnerAbilityComponent;

	UPROPERTY()
	int EffectID;

	UPROPERTY()
	double StartTime;
	
	UPROPERTY()
	double EndTime;

	// Instantly applies effect then exits. Will not tick.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	bool bIsInstant = true;

	// Apply an inversed version of the modifiers at effect end
	// Does not apply to Instant effects
	// Won't work well for periodic effects or anything beyond simple effect modifications
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	bool bNegateEffectAtEnd = true;

	// Delay before the effect starts
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	double Delay = 0;

	// How long the effect lasts
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	double Duration = 0;

	// How often the periodic effect ticks
	// Suggest keeping this above .01
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	double Period = 0;

	// For Period effects, whether first tick should happen immediately
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	bool bPeriodTickAtStart = false;

	// Tag to identify this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag EffectTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTagContainer GrantedTags;

	// Tags that the owner must have to apply and maintain this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTagContainer MustHaveTags;

	// Tags that the owner must not have to apply and maintain this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTagContainer MustNotHaveTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTagContainer GrantedAbilities;

	// If tag is present, periodic effect will not tick. Duration is not affected.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTagContainer PausePeriodicEffect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FGMCAttributeModifier> Modifiers;
	
	inline bool operator==(const FGMCAbilityEffectData& Other) const
	{
		//Todo: Fix this
		return StartTime == Other.StartTime && EndTime == Other.EndTime;
	};

	bool IsValid()
	{
		return GrantedTags != FGameplayTagContainer() || GrantedAbilities != FGameplayTagContainer() || Modifiers.Num() > 0
				|| MustHaveTags != FGameplayTagContainer() || MustNotHaveTags != FGameplayTagContainer();
	}

	FString ToString() const{
		return FString::Printf(TEXT("[id: %d] [Tag: %s] (Duration: %f)"), EffectID, *EffectTag.ToString(), Duration);
	}
};

/**
 * 
 */
class UGMC_AbilitySystemComponent;

UCLASS(Blueprintable, BlueprintType)
class GMCABILITYSYSTEM_API UGMCAbilityEffect : public UObject
{
	GENERATED_BODY()

public:
	EEffectState CurrentState;

	UPROPERTY(EditAnywhere)
	FGMCAbilityEffectData EffectData;

	UFUNCTION(BlueprintCallable)
	void InitializeEffect(FGMCAbilityEffectData InitializationData);
	
	void EndEffect();
	
	virtual void Tick(float DeltaTime);

	UFUNCTION(BlueprintNativeEvent, meta=(DisplayName="Effect Tick"), Category="GMCAbilitySystem")
	void TickEvent(float DeltaTime);
	
	void PeriodTick();
	
	void UpdateState(EEffectState State, bool Force=false);

	bool IsPeriodPaused();
	
	bool bCompleted;

	// Time that the client applied this Effect. Used for when a client predicts an effect, if the server has not
	// confirmed this effect within a time range, the effect will be cancelled.
	float ClientEffectApplicationTime;

protected:
	UPROPERTY(BlueprintReadOnly)
	UGMC_AbilitySystemComponent* SourceAbilityComponent;

	UPROPERTY(BlueprintReadOnly)
	UGMC_AbilitySystemComponent* OwnerAbilityComponent;

private:
	bool bHasStarted;

	// Used for calculating when to tick Period effects
	float PrevPeriodMod = 0;
	
	void CheckState();

	// Tags
	void AddTagsToOwner();
	void RemoveTagsFromOwner();

	void AddAbilitiesToOwner();
	void RemoveAbilitiesFromOwner();

	// Does the owner have any of the tags from the container?
	bool DoesOwnerHaveTagFromContainer(FGameplayTagContainer& TagContainer) const;
	
	bool DuplicateEffectAlreadyApplied();

	// Apply the things that should happen as soon as an effect starts. Tags, instant effects, etc.
	void StartEffect();


public:
	FString ToString() {
		return FString::Printf(TEXT("[name: %s] (State %s) | Started: %d | Period Paused: %d | Data: %s"), *GetName(), *EnumToString(CurrentState), bHasStarted, IsPeriodPaused(), *EffectData.ToString());
	}
};

