// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "GMCAbilitySystem.h"
#include "GMCAttributeModifier.h"
#include "GMCAbilityEffect.generated.h"

class UGMC_AbilitySystemComponent;

UENUM(BlueprintType)
enum class EGMASEffectType : uint8
{
	Instant UMETA(DisplayName = "Instant", ToolTip = "Effect applies instantly, Apply fully and immediatly die, no tick"),
	Ticking UMETA(DisplayName = "Ticking", ToolTip = "Effect applies periodically, Apply each tick multiplied by DeltaTime (Modifiers is Amount per second), and die after Duration or Removal"),
	Persistent UMETA(DisplayName = "Persistent", ToolTip = "Effect applies persistently, Apply fully immediatly, and die after Duration or Removal"),
	Periodic UMETA(DisplayName = "Periodic", ToolTip = "Effect applies periodically, Each interval applu fully the modifier value. Ticking. Die after duration of Removal"),
};

UENUM(BlueprintType)
enum class EGMASEffectState : uint8
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
	uint8 bServerAuth : 1 {false}; // The server will never be acknowledge/predicted

	// True iff this effect was applied via the EGMCAbilityEffectQueueType::ClientAuth path.
	// Mutually exclusive with bServerAuth. Routes GrantedTags to the non-bound
	// ClientAuthActiveTags container so they don't trigger GMC state divergence.
	// Set automatically by ApplyAbilityEffect's case ClientAuth (client side) and by
	// ServerProcessOperation's ClientAuthEffectOperation handler (server side).
	UPROPERTY()
	uint8 bClientAuth : 1 {false};

	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	double StartTime;
	
	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	double EndTime;

	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	double CurrentDuration{0.f};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	EGMASEffectType EffectType = EGMASEffectType::Instant;
	
	// Apply an inversed version of the modifiers at effect end
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem", meta=(EditCondition = "EffectType == EGMASEffectType::Ticking || EffectType == EGMASEffectType::Persistent || EffectType == EGMASEffectType::Periodic", EditConditionHides))
	bool bNegateEffectAtEnd = false;

	// Delay before the effect starts
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	double Delay = 0;

	// Periodic will tick immediatly if true
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem" , meta=(EditCondition = "EffectType == EGMASEffectType::Periodic", EditConditionHides))
	bool bPeriodicFirstTick = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem" , meta=(EditCondition = "EffectType == EGMASEffectType::Periodic", EditConditionHides, ClampMin = "0.1", UIMin = "0.1"))
	float PeriodicInterval = 1.f;

	// How long the effect lasts, 0 for infinite
	// Does nothing if effect is instant
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem", meta=(EditCondition = "EffectType == EGMASEffectType::Ticking || EffectType == EGMASEffectType::Persistent || EffectType == EGMASEffectType::Periodic", EditConditionHides))
	double Duration = 0;
	
	// Time in seconds that the client has to apply itself an external effect before the server will force it. If this time is reach, a rollback is likely to happen.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem", AdvancedDisplay)
	float ClientGraceTime = 1.f;

	// Tag to identify this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTag EffectTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer GrantedTags;

	// When true (default), GrantedTags survive on the owner while any other same-EffectTag
	// instance is still alive — only the last instance to end clears the tags. Required for
	// stackable effects: FGameplayTagContainer is set-like and can't track stack counts, so
	// per-instance tag removal would yank the tag while siblings still depend on it.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	bool bPreserveGrantedTagsIfMultiple = true;

	// Opt-in single-instance protection by EffectTag (exact match). New Apply is
	// rejected if a same-tag effect is functionally active; if the match is in its
	// bilateral PredictedEnd defer window, the new Apply succeeds and the old is
	// replaced — server force-ends immediately, client suspends-then-finalizes-or-
	// revives based on the successor's server verdict (Validated vs Timeout).
	// Empty EffectTag disables the check.
	//
	// Drain-rate symmetry between client and server during the recovery window is
	// exact for Ticking modifiers (continuous flow). Periodic/Persistent effects
	// can produce a bounded drift up to one modifier-worth — prefer Ticking, or
	// accept the bounded mismatch.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	bool bUniqueByEffectTag = false;

	// Tags that the owner must have to apply this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer ApplicationMustHaveTags;

	// Tags that the owner must not have to apply this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer ApplicationMustNotHaveTags;

	// Tags that the owner must have to apply and maintain this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer MustHaveTags;

	// Tags that the owner must not have to apply and maintain this effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer MustNotHaveTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer GrantedAbilities;

	// If tag is present, this effect will not tick. Duration is not affected.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer PauseEffect;

	// On activation, will end ability present in this container
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer CancelAbilityOnActivation;

	// When this effect end, it will end ability present in this container
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer CancelAbilityOnEnd;

	// Effect classes to apply when this effect ends. Each entry is applied via
	// ApplyAbilityEffectShort using a queue type chosen at runtime: Predicted while inside
	// a GMC tick (movement/ancillary) or Standalone, PredictedQueued otherwise. Applies on
	// every side that runs EndEffect (server + owning client) so the GMAS predict/validate
	// pipeline handles confirmation symmetrically.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem|Chain")
	TArray<TSubclassOf<UGMCAbilityEffect>> ApplyEffectOnEnd;

	// EffectTags to remove from the owner when this effect ends. Each tag matches active
	// effects via RemoveEffectByTagSafe (NumToRemove = -1, removes all matching). Same
	// queue type policy as ApplyEffectOnEnd.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem|Chain", meta = (Categories = "Effect"))
	FGameplayTagContainer RemoveEffectOnEnd;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	TArray<FGMCAttributeModifier> Modifiers;
	
	bool IsValid() const
	{
		// bUniqueByEffectTag carries runtime intent on its own (single-instance protection
		// keyed on EffectTag); EffectTag's presence is similarly meaningful as identity for
		// query/removal paths even without granted content. Both count as "valid override"
		// against the CDO defaults at apply time.
		return GrantedTags != FGameplayTagContainer() || GrantedAbilities != FGameplayTagContainer() || Modifiers.Num() > 0
				|| MustHaveTags != FGameplayTagContainer() || MustNotHaveTags != FGameplayTagContainer()
				|| bUniqueByEffectTag || EffectTag.IsValid();
	}

	FString ToString() const{
		return FString::Printf(TEXT("[id: %d] [Tag: %s] (Dur: %.3lf) (CurDur: %.3lf)"), EffectID, *EffectTag.ToString(), Duration, CurrentDuration);
	}

	// query stuff
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	// Container for a more generalized definition of effects
	FGameplayTagContainer EffectDefinition;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	// query must match on effect activation
	FGameplayTagQuery ActivationQuery;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem")
	// query must be maintained throughout effect
	FGameplayTagQuery MustMaintainQuery;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem", meta = (DisplayName = "End Ability On Activation Via Definition Query"))
	// end ability on effect activation if definition matches query
	FGameplayTagQuery EndAbilityOnActivationQuery;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMCAbilitySystem", meta = (DisplayName = "End Ability On End Via Definition Query"))
	// end ability on effect end if definition matches query
	FGameplayTagQuery EndAbilityOnEndQuery;
};

/**
 * 
 */
class UGMC_AbilitySystemComponent;

UCLASS(Blueprintable, BlueprintType)
class GMCABILITYSYSTEM_API UGMCAbilityEffect : public UObject
{
	GENERATED_BODY()

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	

public:
	EGMASEffectState CurrentState = EGMASEffectState::Initialized;

	UPROPERTY(EditAnywhere, Category = "GMCAbilitySystem")
	FGMCAbilityEffectData EffectData;

	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem")
	bool bCallOnAttributeModifierApplication = false;

	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	void InitializeEffect(FGMCAbilityEffectData InitializationData);


	/**
	 * Called when an attribute modifier is applied.
	 *
	 * ⚠️ Note:
	 * Attribute changes are applied with a one-frame delay by design.
	 * This means that if you add (for example) health and check the current health immediately,
	 * the modifier’s effect will not yet be visible during the same frame.
	 *
	 * @param Modifier The attribute modifier that was applied.
	 */
	UFUNCTION(BlueprintImplementableEvent)
	void K2_OnAttributeModifierApplication(const FGMCAttributeModifier& Modifier);

	virtual void OnAttributeModifierApplication(const FGMCAttributeModifier& Modifier);

	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	void EndEffect();

	virtual void BeginDestroy() override;
	
	virtual void Tick(float DeltaTime);

	int32 CalculatePeriodicTicksBetween(float Period, float StartActionTimer, float EndActionTimer);

	// Return the current duration of the effect
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Effects")
	float GetCurrentDuration() const { return EffectData.CurrentDuration; }

	// Return the effect data struct of targeted effect
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Effects")
	FGMCAbilityEffectData GetEffectData() const { return EffectData; }

	// Return the total duration of the effect
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Effects")
	float GetEffectTotalDuration() const { return EffectData.Duration; }

	// Return the current remaining duration of the effect
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Effects")
	float GetEffectRemainingDuration() const { return EffectData.Duration - EffectData.CurrentDuration; }

	UFUNCTION(BlueprintNativeEvent, meta=(DisplayName="Effect Tick"), Category="GMCAbilitySystem")
	void TickEvent(float DeltaTime);

	// Dynamic Condition allow you to avoid applying the attribute modifier if a condition is not met, for example, a sprint effect with attribute cost when the player is below a certain speed.
	// However, this is not stopping the effect.
	UFUNCTION(BlueprintNativeEvent, meta=(DisplayName="Dynamic Condition"), Category="GMCAbilitySystem")
	bool AttributeDynamicCondition() const;
	
	void PeriodTick();

	UFUNCTION(BlueprintNativeEvent, meta=(DisplayName="Period Tick"), Category="GMCAbilitySystem")
	void PeriodTickEvent();
	
	void UpdateState(EGMASEffectState State, bool Force=false);

	virtual bool IsPaused();

	bool IsEffectModifiersRegisterInHistory() const;
	
	float ProcessCustomModifier(const TSubclassOf<UGMCAttributeModifierCustom_Base>& MCClass, const FAttribute* attribute);

	bool bCompleted;

	// Bilateral defer absolute timestamp for Predicted Remove on Ticking/Periodic effects.
	// Both sides arm `ActionTimer + ClientGraceTime` at the same logical move tick so they
	// end on the same logical tick regardless of DeltaTime / framerate / replay count.
	// -1.0 = not armed; >= 0 = armed, end when ActionTimer reaches the value.
	double EndAtActionTimer { -1.0 };

	// Set on the OLD instance during a client-side bUniqueByEffectTag REPLACE. Tick early-
	// returns (no modifier application) but tags / abilities / EndAtActionTimer survive.
	// Cleared by the polling block in TickActiveEffects when the successor reaches
	// Validated (real EndEffect runs) or Timeout (OLD is revived).
	bool bPendingDeathBySuccessor = false;

	// Time that the client applied this Effect. Used for when a client predicts an effect, if the server has not
	// confirmed this effect within a time range, the effect will be cancelled.
	float ClientEffectApplicationTime;
	
	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	void GetOwnerActor(AActor*& OwnerActor) const;

	AActor* GetOwnerActor() const;

	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	UGMC_AbilitySystemComponent* GetOwnerAbilityComponent() const { return OwnerAbilityComponent; }

protected:

	UPROPERTY(Transient)
	TMap<TSubclassOf<UGMCAttributeModifierCustom_Base>, UGMCAttributeModifierCustom_Base*> CustomModifiersInstances;

	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	UGMC_AbilitySystemComponent* OwnerAbilityComponent = nullptr;

	// Apply the things that should happen as soon as an effect starts. Tags, instant effects, etc.
	virtual void StartEffect();

private:
	bool bHasStarted;
	bool bHasAppliedEffect;
	
	void CheckState();

	// Tags
	void AddTagsToOwner();

	// bPreserveOnMultipleInstances: If true, will not remove tags if there are multiple instances of the same effect
	void RemoveTagsFromOwner(bool bPreserveOnMultipleInstances = true);

	void AddAbilitiesToOwner();
	void RemoveAbilitiesFromOwner();
	void EndActiveAbilitiesFromOwner(const FGameplayTagContainer& TagContainer);

	// Does the owner have any of the tags from the container?
	bool DoesOwnerHaveTagFromContainer(FGameplayTagContainer& TagContainer) const;
	
	void EndActiveAbilitiesByDefinitionQuery(FGameplayTagQuery);

	
public:

	// Blueprint Event for when the effect starts
	UFUNCTION(BlueprintImplementableEvent)
	void StartEffectEvent();

	UFUNCTION(BlueprintImplementableEvent)
	void EndEffectEvent();

	
	FString ToString() {
		return FString::Printf(TEXT("[name: %s] (%s) | %s | %s | Data: %s"), *GetName().Right(30), *EnumToString(CurrentState), bHasStarted ? TEXT("Started") : TEXT("Not Started"), IsPaused() ? TEXT("Paused") : TEXT("Running"), *EffectData.ToString());
	}

	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem|Effects|Queries")
	void ModifyMustMaintainQuery(const FGameplayTagQuery& NewQuery);

	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem|Effects|Queries")
	void ModifyEndAbilitiesOnEndQuery(const FGameplayTagQuery& NewQuery);
};


