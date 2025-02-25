// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GMCAbilityEffectTypes.h"
#include "UObject/Object.h"
#include "Attributes/GMCAttributeModifier.h"
#include "GMCAbilityEffect.generated.h"

class UGMC_AbilitySystemComponent;


UENUM(BlueprintType)
enum class EGMASEffectType : uint8
{
	Instant,  // Applies Instantly
	Duration, // Lasts for X time
	Infinite  // Lasts forever
};

UENUM(BlueprintType)
enum class EGMASEffectState : uint8
{
	Initialized,  // Applies Instantly
	Started, // Lasts for X time
	Ended  // Lasts forever
};

UENUM()
enum class EAbilityEffectStackingDurationPolicy : uint8
{
	/** The duration of the effect will be refreshed from any successful stack application */
	RefreshOnSuccessfulApplication,

	/** The duration of the effect will never be refreshed */
	NeverRefresh,
};



/** Enumeration of policies for dealing with the period of a gameplay effect while stacking */
UENUM()
enum class EAbilityEffectStackingPeriodPolicy : uint8
{
	/** Any progress toward the next tick of a periodic effect is discarded upon any successful stack application */
	ResetOnSuccessfulApplication,

	/** The progress toward the next tick of a periodic effect will never be reset, regardless of stack applications */
	NeverReset,
};
/** Enumeration of policies for dealing gameplay effect stacks that expire (in duration based effects). */
UENUM()
enum class EAbilityEffectStackingExpirationPolicy : uint8
{
	/** The entire stack is cleared when the active gameplay effect expires  */
	ClearEntireStack,

	/** The current stack count will be decremented by 1 and the duration refreshed. The GE is not "reapplied", just continues to exist with one less stacks. */
	RemoveSingleStackAndRefreshDuration,

	/** The duration of the gameplay effect is refreshed. This essentially makes the effect infinite in duration. This can be used to manually handle stack decrements via OnStackCountChange callback */
	RefreshDuration,
};
UENUM()
enum class EAbilityEffectStackingType : uint8
{
	/** No stacking. Multiple applications of this AbilityEffect are treated as separate instances. */
	None,
	/** Each caster has its own stack. Not Implemented yet*/
	AggregateBySource,
	/** Each target has its own stack. */
	AggregateByTarget,
};


USTRUCT(BlueprintType)
struct FGameEffectCue
{
	GENERATED_USTRUCT_BODY()

	FGameEffectCue()
		: MinLevel(0.f)
		, MaxLevel(0.f)
	{
	}

	FGameEffectCue(const FGameplayTag& InTag, float InMinLevel, float InMaxLevel)
		: MinLevel(InMinLevel)
		, MaxLevel(InMaxLevel)
	{
		GameCueTags.AddTag(InTag);
	}

	/** The attribute to use as the source for cue magnitude. If none use level */
	////	FAttribute MagnitudeAttribute;
	//
	/** The minimum level that this Cue supports */
	UPROPERTY(EditDefaultsOnly, Category = GameCue)
	float	MinLevel;

	/** The maximum level that this Cue supports */
	UPROPERTY(EditDefaultsOnly, Category = GameCue)
	float	MaxLevel;

	/** Tags passed to the gameplay cue handler when this cue is activated */
	UPROPERTY(EditDefaultsOnly, Category = GameCue, meta = (Categories="GameCue"))
	FGameplayTagContainer GameCueTags;

	float NormalizeLevel(float InLevel)
	{
		float Range = MaxLevel - MinLevel;
		if (Range <= KINDA_SMALL_NUMBER)
		{
			return 1.f;
		}

		return FMath::Clamp((InLevel - MinLevel) / Range, 0.f, 1.0f);
	}
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
							StackCount(1),
	                         StartTime(0),
	                         EndTime(0)
	{
	}

	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	UGMC_AbilitySystemComponent* SourceAbilityComponent;

	UPROPERTY()
	UGMC_AbilitySystemComponent* OwnerAbilityComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	int EffectID;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	int StackCount;

	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	double StartTime;
	
	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	double EndTime;

	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	double CurrentDuration{0.f};

	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	TArray<FGameEffectCue>	GameCues;
	

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

	// Time in seconds that the client has to apply itself an external effect before the server will force it. If this time is reach, a rollback is likely to happen.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem", AdvancedDisplay)
	float ClientGraceTime = 1.f;
	
	UPROPERTY()
	int LateApplicationID = -1;

	// Tag to identify this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTag EffectTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer GrantedTags;

	// Whether to preserve the granted tags if multiple instances of the same effect are applied
	// If false, will remove all stacks of the tag
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	bool bPreserveGrantedTagsIfMultiple = false;

	// Tags that the owner must have to apply this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer ApplicationMustHaveTags;

	// Tags that the owner must not have to apply this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer ApplicationMustNotHaveTags;

	// Tags that the target must have to apply and maintain this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer MustHaveTags;

	// Tags that the target must not have to apply and maintain this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer MustNotHaveTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer GrantedAbilities;

	// If tag is present, periodic effect will not tick. Duration is not affected.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer PausePeriodicEffect;

	// On activation, will end ability present in this container
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer CancelAbilityOnActivation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	TArray<FGMCAttributeModifier> Modifiers;


	
	inline bool operator==(const FGMCAbilityEffectData& Other) const
	{
		//Todo: Fix this
		return StartTime == Other.StartTime && EndTime == Other.EndTime;
	};

	bool IsValid() const
	{
		return GrantedTags != FGameplayTagContainer() || GrantedAbilities != FGameplayTagContainer() || Modifiers.Num() > 0
				|| MustHaveTags != FGameplayTagContainer() || MustNotHaveTags != FGameplayTagContainer();
	}

	FString ToString() const{
		return FString::Printf(TEXT("[id: %d] [Tag: %s] (Duration: %.3lf) (CurrentDuration: %.3lf)"), EffectID, *EffectTag.ToString(), Duration, CurrentDuration);
	}
};



 
USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FAbilityEffectSpec
{
	GENERATED_USTRUCT_BODY()

	FAbilityEffectSpec();
};



USTRUCT()
struct GMCABILITYSYSTEM_API FAbilityEffectSpecForRPC
{
	GENERATED_USTRUCT_BODY()
	FAbilityEffectSpecForRPC();
	UPROPERTY()
	TObjectPtr<const UGMCAbilityEffect> Def;
	UPROPERTY()
	float Level;
	UPROPERTY()
	float AbilityLevel;

	float GetLevel() const
	{
		return Level;
	}

	float GetAbilityLevel() const
	{
		return AbilityLevel;
	}
};

/**
 * FGameplayEffectCue
 *	This is a cosmetic cue that can be tied to a UGameplayEffect. 
 *  This is essentially a GameplayTag + a Min/Max level range that is used to map the level of a GameplayEffect to a normalized value used by the GameCue system.



/**
 * 
 */
class UGMC_AbilitySystemComponent;

UCLASS(Blueprintable, BlueprintType)
class GMCABILITYSYSTEM_API UGMCAbilityEffect : public UObject
{
	GENERATED_BODY()

public:
	EGMASEffectState CurrentState;

	UPROPERTY(EditAnywhere, Category = "GMCAbilitySystem")
	FGMCAbilityEffectData EffectData;
	FActiveAbilityEffectEvents EventSet;
	// ----------------------------------------------------------------------
	//	Stacking
	// ----------------------------------------------------------------------


	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Stacking)
	EAbilityEffectStackingType	StackingType;
	
	/** Stack limit for StackingType */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Stacking, meta = (EditConditionHides, EditCondition = "StackingType != EAbilityEffectStackingType::None"))
	int32 StackLimitCount;

	/** Policy for how the effect duration should be refreshed while stacking */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Stacking, meta = (EditConditionHides, EditCondition = "StackingType != EAbilityEffectStackingType::None"))
	EAbilityEffectStackingDurationPolicy StackDurationRefreshPolicy;

	/** Policy for how the effect period should be reset (or not) while stacking */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Stacking, meta = (EditConditionHides, EditCondition = "StackingType != EAbilityEffectStackingType::None"))
	EAbilityEffectStackingPeriodPolicy StackPeriodResetPolicy;

	/** Policy for how to handle duration expiring on this gameplay effect */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Stacking, meta = (EditConditionHides, EditCondition = "StackingType != EAbilityEffectStackingType::None"))
	EAbilityEffectStackingExpirationPolicy StackExpirationPolicy;

	UPROPERTY()
	FAbilityEffectContextHandle ContextHandle;
	
	int32 EffectStackCount = 1;

	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	virtual void InitializeEffect(FGMCAbilityEffectData InitializationData);
	void HandleStacking(UGMCAbilityEffect* NewEffect);

	virtual void EndEffect();

	virtual void BeginDestroy() override;
	
	virtual void Tick(float DeltaTime);

	// Return the current duration of the effect
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Abilities")
	float GetCurrentDuration() const { return EffectData.CurrentDuration; }

	// Return the current duration of the effect
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Abilities")
	FGMCAbilityEffectData GetEffectData() const { return EffectData; }


	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Abilities")
	int GetEffectHandleID() const {    return EffectData.EffectID != 0 ? EffectData.EffectID : -1;}

	// Return the current duration of the effect
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Abilities")
	float GetEffectTotalDuration() const { return EffectData.Duration; }


	void OnStackCountChange(UGMCAbilityEffect* ActiveEffect, int32 OldStackCount, int32 NewStackCount);

	
	/** Sets the stack count for this GE to NewStackCount if stacking is supported. */
	void SetStackCount(int32 NewStackCount);

	/** Returns the stack count for this GE spec. */
	int32 GetStackCount() const;

	int32 GetStackLimitCount() const;


	UFUNCTION(BlueprintNativeEvent, meta=(DisplayName="Effect Tick"), Category="GMCAbilitySystem")
	void TickEvent(float DeltaTime);

	// Dynamic Condition allow you to avoid applying the attribute modifier if a condition is not met, for example, a sprint effect with attribute cost when the player is below a certain speed.
	// However, this is not stopping the effect.
	UFUNCTION(BlueprintNativeEvent, meta=(DisplayName="Dynamic Condition"), Category="GMCAbilitySystem")
	bool AttributeDynamicCondition() const;

	/** If true, cues will only trigger when GE modifiers succeed being applied (whether through modifiers or executions) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameCues")
	bool bRequireModifierSuccessToTriggerCues;
	/** If true, GameCues will only be triggered for the first instance in a stacking GameplayEffect. */
	UPROPERTY(EditDefaultsOnly, Category = "GameCues")
	bool bSuppressStackingCues;
	
	/** Cues to trigger non-simulated reactions in response to this GameplayEffect such as sounds, particle effects, etc */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameCues")
	TArray<FGameEffectCue>	GameCues;

	
	virtual void PeriodTick();
	
	void UpdateState(EGMASEffectState State, bool Force=false);

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

	// Tags
	void AddTagsToOwner();

	// bPreserveOnMultipleInstances: If true, will not remove tags if there are multiple instances of the same effect
	void RemoveTagsFromOwner(bool bPreserveOnMultipleInstances = true);

	void AddAbilitiesToOwner();
	void PlayQueue();

	void RemoveAbilitiesFromOwner();
	void EndActiveAbilitiesFromOwner();

	// Does the owner have any of the tags from the container?
	bool DoesOwnerHaveTagFromContainer(FGameplayTagContainer& TagContainer) const;
	
	bool DuplicateEffectAlreadyApplied();

	// Apply the things that should happen as soon as an effect starts. Tags, instant effects, etc.
	virtual void StartEffect();

	bool bHasStarted;


private:
	// Used for calculating when to tick Period effects
	float PrevPeriodMod = 0;
	
	void CheckState();

public:
	FString ToString()
	{
		return FString::Printf(TEXT("[name: %s] (State rework code) | Started: %d | Period Paused: %d | Data: %s"), 
							   *GetName(), 
							   //*EnumToString(CurrentState), 
							   bHasStarted, 
							   IsPeriodPaused(), 
							   *EffectData.ToString());
	}
};

