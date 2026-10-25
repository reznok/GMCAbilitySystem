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

	// Persistent only: re-evaluate each modifier's Conditions every tick and refresh its contribution
	// (a constant buff/debuff that toggles on/off, or swaps its value source, with a tag — without
	// re-applying the effect). Requires bNegateEffectAtEnd: the modifiers must be temporal/removable,
	// not baked into RawValue. No effect on Instant/Ticking/Periodic.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem", meta=(DisplayName="Recheck Conditions Each Tick (Persistent)", ToolTip="Persistent effects only: re-checks every modifier's Conditions each tick and applies or removes the buff as they change, without re-applying the effect. Requires 'Negate Effect At End'.", EditCondition = "EffectType == EGMASEffectType::Persistent && bNegateEffectAtEnd", EditConditionHides))
	bool bReevaluateConditionsWhilePersistent = false;

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
	
	// Per-effect override for the bilateral defer window (seconds) applied when a Ticking/Periodic
	// effect is removed: both client and server arm EndAtActionTimer = ActionTimer + this value so
	// each side ends on the same logical move tick.
	//
	// Sentinel semantics: 0 means "use the project-wide default" from
	// `UGMASNetworkTimingSettings::DefaultClientGraceTime` (Project Settings → GMC Ability System →
	// Network Timing, default 0: the effect ends at the next move).
	// Set this to >0 only when an individual effect needs a longer window.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem", AdvancedDisplay)
	float ClientGraceTime = 0.f;

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
	// replaced — both sides end the old one at once.
	// Empty EffectTag disables the check.
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

// Part of a GMC prediction tick where an effect transition ran. A client replay re-runs the transition
// at the same move and part, because the replay rewinds bound tags and attributes but not the effects.
enum class EGMASReplayPhase : uint8
{
	None,           // outside a prediction tick
	PreTick,        // before the move: an out-of-tick transition, stamped by the next local move
	BeforeEffects,  // queued operations, the move operation, starting effects
	Abilities,      // task data and TickActiveAbilities
	InEffects,      // TickActiveEffects
	AfterEffects,   // attribute processing and ability cleanup
	PostTick        // the ancillary tick: after the prediction tick, before GMC saves the move output
};

// Move and position of one effect transition (start or end) in the client timeline.
struct FGMASEffectReplayStamp
{
	double Time = 0.0;
	int32 Seq = 0;
	// Replay pass that recorded it; 0 = a local move. A pass never re-runs its own transitions.
	int32 PassId = 0;
	// Value of the pass counter when the transition ran. Tells if a replay ran before a pending stamp resolved.
	int32 PassCountAtRecord = 0;
	// Last replay pass that re-ran it. Sub-stepped moves tick several times with the same move time.
	int32 ReplayedInPass = 0;
	EGMASReplayPhase Phase = EGMASReplayPhase::None;
	bool bSet = false;
	bool bPending = false;

	// True when the stamp is at or before the given point of the replayed timeline.
	bool IsAtOrBefore(double MoveTime, EGMASReplayPhase InPhase) const
	{
		return bSet && (Time < MoveTime || (Time == MoveTime && Phase <= InPhase));
	}
};

// One temporal attribute entry of an effect, kept for the client replay journal.
struct FGMASReplayTemporalEntry
{
	FGameplayTag AttributeTag;
	int32 ApplicationIndex = 0;
	float Value = 0.f;
	double ActionTimer = 0.0;
	uint8 Kind = 0;
};

// One write to the bound list of active effect IDs, kept for the client replay journal.
struct FGMASReplayBoundEffectIDWrite
{
	FGMASEffectReplayStamp Stamp;
	int32 EffectID = -1;
	bool bAdd = false;
};

UCLASS(Blueprintable, BlueprintType)
class GMCABILITYSYSTEM_API UGMCAbilityEffect : public UObject
{
	GENERATED_BODY()

public:
	// Public on purpose: GENERATED_BODY() resets access to private, which silently made this
	// override private and broke Super::PostEditChangeProperty() calls in derived classes
	// (C2248). UObject declares it public — keep the same visibility.
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	EGMASEffectState CurrentState = EGMASEffectState::Initialized;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GMCAbilitySystem")
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
	virtual void EndEffect();

	virtual void BeginDestroy() override;
	
	virtual void Tick(float DeltaTime);

	int32 CalculatePeriodicTicksBetween(float Period, float StartActionTimer, float EndActionTimer);

	// Return the current duration of the effect
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Effects")
	float GetCurrentDuration() const { return EffectData.CurrentDuration; }

	// Return the effect data struct of targeted effect
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Effects")
	FGMCAbilityEffectData GetEffectData() const { return EffectData; }

	/**
	 * Static helper: copy the EffectData struct off the given effect class's CDO.
	 * Returns an empty struct if EffectClass is null (caller's Apply will fall
	 * back to CDO via FGMCAbilityEffectData::IsValid()==false).
	 *
	 * Intended for the "start from CDO + layer per-cast overrides" pattern:
	 *
	 *   Get Default Effect Data: GE_Slow
	 *     → Set Members in Struct (Duration: 5.0)
	 *       → Apply Ability Effect (InitializationData = struct)
	 *
	 * One-node convenience vs the two-step Get Class Defaults + struct field
	 * extraction. The Apply call's IsValid() check still passes (CDO content
	 * present), so the modified struct is used — not the CDO again.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Effects",
	          meta=(DisplayName="Get Default Effect Data"))
	static FGMCAbilityEffectData GetDefaultEffectData(TSubclassOf<UGMCAbilityEffect> EffectClass);

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
	// -1.0 = not armed; >= 0 = armed, end at the first move with ActionTimer strictly above the value.
	double EndAtActionTimer { -1.0 };

	// Remove made between moves: the arm waits for the next local move, where the other side runs the Remove.
	// Holds the grace to arm with; -1.0 = none.
	double PendingEndGrace { -1.0 };

	// Time that the client applied this Effect. Used for when a client predicts an effect, if the server has not
	// confirmed this effect within a time range, the effect will be cancelled.
	float ClientEffectApplicationTime;

	// --- Client replay journal ---
	// A replay rewinds bound tags and attributes, but this instance survives it and its activation operation
	// is skipped as consumed. The stamps let the replay re-run its bound writes at the move that made them.
	FGMASEffectReplayStamp ReplayCreatedStamp;
	FGMASEffectReplayStamp ReplayStartStamp;
	FGMASEffectReplayStamp ReplayEndStamp;

	// Last move whose tick applied Ticking, Periodic or re-evaluated modifiers.
	double ReplayLastModifierTickTime = 0.0;
	bool bHasReplayLastModifierTick = false;

	bool HasAppliedEffect() const { return bHasAppliedEffect; }

	// True while the replayed point precedes the creation or the start of this effect: its tick must not run yet.
	bool IsReplayedBeforeStart(double MoveTime, EGMASReplayPhase Phase) const;

	// Bound writes of StartEffect / EndEffect only: no events, ability changes, chained effects or broadcasts.
	void ReplayStartWrites();
	void ReplayEndWrites();

	// Ended effect still active at a replayed move: re-applies that move's tick modifiers.
	void ReplayGhostTick(float DeltaTime);

	// Ended effect: puts back the temporal entries it held at the replay source move.
	void RestoreGhostTemporalModifiersUpTo(double MaxActionTimer);

	// Removes every temporal entry of this effect. A released ghost must leave no entry behind.
	void RemoveAllTemporalModifiers();

	// Moves the entries of a start that ran outside a prediction tick to the move that stamped it.
	void RestampStartTemporalModifiers(double NewTime);

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

	// Bound tags that StartEffect added and EndEffect removed, so a replay repeats the same decision.
	TArray<FGameplayTag> ReplayAddedTags;
	TArray<FGameplayTag> ReplayRemovedTags;
	// Same for the granted abilities: GrantedAbilityTags is bound too.
	TArray<FGameplayTag> ReplayGrantedAbilityTags;
	TArray<FGameplayTag> ReplayRemovedAbilityTags;

	// Temporal entries written by StartEffect, and all temporal entries held when EndEffect ran.
	TArray<FGMASReplayTemporalEntry> ReplayStartTemporalModifiers;
	TArray<FGMASReplayTemporalEntry> ReplayEndTemporalModifiers;

	void CaptureTemporalModifiers(TArray<FGMASReplayTemporalEntry>& OutEntries) const;
	void RestoreTemporalModifiers(const TArray<FGMASReplayTemporalEntry>& Entries, double MaxActionTimer, bool bInclusive) const;

	// Ticking / Periodic / re-evaluated modifier application, shared by Tick and the replay of an ended effect.
	// bLiveTick: fires the Blueprint events and records the tick for the replay journal.
	void ApplyTickModifiers(float DeltaTime, bool bLiveTick);

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
	UFUNCTION(BlueprintNativeEvent)
	void StartEffectEvent();

	UFUNCTION(BlueprintNativeEvent)
	void EndEffectEvent();

	
	FString ToString() {
		return FString::Printf(TEXT("[name: %s] (%s) | %s | %s | Data: %s"), *GetName().Right(30), *EnumToString(CurrentState), bHasStarted ? TEXT("Started") : TEXT("Not Started"), IsPaused() ? TEXT("Paused") : TEXT("Running"), *EffectData.ToString());
	}

	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem|Effects|Queries")
	void ModifyMustMaintainQuery(const FGameplayTagQuery& NewQuery);

	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem|Effects|Queries")
	void ModifyEndAbilitiesOnEndQuery(const FGameplayTagQuery& NewQuery);
};


