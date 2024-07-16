// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "GMCAbilitySystem.h"
#include "Attributes/GMCAttributeModifier.h"
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

// Container for exposing the attribute modifier to blueprints
UCLASS()
class GMCABILITYSYSTEM_API UGMCAttributeModifierContainer : public UObject
{
	GENERATED_BODY()
	
public:
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
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

	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
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
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	bool bIsInstant = true;

	// Apply an inversed version of the modifiers at effect end
	UPROPERTY()
	bool bNegateEffectAtEnd = false;

	// Delay before the effect starts
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	double Delay = 0;

	// How long the effect lasts
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	double Duration = 0;

	// How often the periodic effect ticks
	// Suggest keeping this above .01
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	double Period = 0;

	// For Period effects, whether first tick should happen immediately
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	bool bPeriodTickAtStart = false;

	// Tag to identify this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTag EffectTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer GrantedTags;

	// Tags that the owner must have to apply this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer ApplicationMustHaveTags;

	// Tags that the owner must not have to apply this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer ApplicationMustNotHaveTags;

	// Tags that the owner must have to apply and maintain this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer OngoingMustHaveTags;

	// Tags that the owner must not have to apply and maintain this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer OngoingMustNotHaveTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer GrantedAbilities;

	// If tag is present, periodic effect will not tick. Duration is not affected.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer PausePeriodicEffect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	TArray<FGMCAttributeModifier> Modifiers;
	
	inline bool operator==(const FGMCAbilityEffectData& Other) const
	{
		//Todo: Fix this
		return StartTime == Other.StartTime && EndTime == Other.EndTime;
	};

	bool IsValid()
	{
		return GrantedTags != FGameplayTagContainer() || GrantedAbilities != FGameplayTagContainer() || Modifiers.Num() > 0
				|| OngoingMustHaveTags != FGameplayTagContainer() || OngoingMustNotHaveTags != FGameplayTagContainer();
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

	UPROPERTY(EditAnywhere, Category = "GMCAbilitySystem")
	FGMCAbilityEffectData EffectData;

	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	void InitializeEffect(FGMCAbilityEffectData InitializationData);
	
	void EndEffect();
	
	virtual void Tick(float DeltaTime);

	UFUNCTION(BlueprintNativeEvent, meta=(DisplayName="Effect Tick"), Category="GMCAbilitySystem")
	void TickEvent(float DeltaTime);
	
	void PeriodTick();
	
	void UpdateState(EEffectState State, bool Force=false);

	virtual bool IsPeriodPaused();
	
	bool bCompleted;

	// Time that the client applied this Effect. Used for when a client predicts an effect, if the server has not
	// confirmed this effect within a time range, the effect will be cancelled.
	float ClientEffectApplicationTime;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	UGMC_AbilitySystemComponent* SourceAbilityComponent;

	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
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

