// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "GMCAbilityEffect.generated.h"

class UGMC_AbilityComponent;

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

USTRUCT(BlueprintType)
struct FGMCAttributeModifier
{
	GENERATED_BODY()

	FGMCAttributeModifier()
	{
		Value = 0;
	}

	// String name of the attribute to modify, ie: Health, Mana, etc
	// Needs to match the attribute name in the AttributeSet
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FName AttributeName;

	// Value to modify the attribute by
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	float Value;

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
	                         EndTime(0),
	                         Delay(0),
	                         Duration(0)
	{
	}

	UPROPERTY(BlueprintReadWrite)
	UGMC_AbilityComponent* SourceAbilityComponent;

	UPROPERTY()
	UGMC_AbilityComponent* OwnerAbilityComponent;

	UPROPERTY()
	int EffectID;

	UPROPERTY()
	double StartTime;
	
	UPROPERTY()
	double EndTime;

	// Instantly applies effect then exits. Will not tick.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	bool bIsInstant;

	// Apply an inversed version of the modifiers at effect end
	// Does not apply to Instant effects
	// Won't work well for periodic effects or anything beyond simple effect modifications
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	bool bNegateEffectAtEnd = true;

	// Delay before the effect starts
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	double Delay;

	// How long the effect lasts
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	double Duration;

	// How often the periodic effect ticks
	// Suggest keeping this above .01
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	double Period;

	// For Period effects, whether first tick should happen immediately
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	bool bPeriodTickAtStart;

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
};

/**
 * 
 */
class UGMC_AbilityComponent;

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
	
	virtual void Tick(float DeltaTime, bool bIsReplayingPrediction);

	UFUNCTION(BlueprintNativeEvent, meta=(DisplayName="Effect Tick"), Category="GMCAbilitySystem")
	void TickEvent(float DeltaTime);

	UFUNCTION(BlueprintNativeEvent, meta=(DisplayName="Periodic Tick"), Category="GMCAbilitySystem")
	void PeriodTickEvent();
	
	void UpdateState(EEffectState State, bool Force=false);
	
	bool CompletedAndServerConfirmed();
	
	bool bCompleted;
	bool bServerConfirmed;

protected:
	UPROPERTY(BlueprintReadOnly)
	UGMC_AbilityComponent* SourceAbilityComponent;

	UPROPERTY(BlueprintReadOnly)
	UGMC_AbilityComponent* OwnerAbilityComponent;

private:
	bool bHasStarted;

	TArray<double> PeriodTickTimes;
	float PeriodTimer = 0;

	// Used for calculating when to tick Period effects
	int PrevPeriodMod = 0;
	
	void CheckState();

	// Tags
	void AddTagsToOwner();
	void RemoveTagsFromOwner();

	void AddAbilitiesToOwner();
	void RemoveAbilitiesFromOwner();

	bool CheckMustHaveTags();
	bool CheckMustNotHaveTags();
	bool DuplicateEffectAlreadyApplied();

	// Apply the things that should happen as soon as an effect starts. Tags, instant effects, etc.
	void StartEffect();
};

