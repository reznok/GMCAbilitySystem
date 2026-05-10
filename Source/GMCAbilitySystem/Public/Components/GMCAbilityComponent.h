
#pragma once

#include "CoreMinimal.h"
#include "GameplayTasksComponent.h"
#include "Attributes/GMCAttributes.h"
#include "GMCMovementUtilityComponent.h"
#include "Ability/GMCAbilityData.h"
#include "Ability/GMCAbilityMapData.h"
#include "Ability/Tasks/GMCAbilityTaskData.h"
#include "Effects/GMCAbilityEffect.h"
#include "Components/ActorComponent.h"
#include "Utility/GMASBoundQueueV2.h"
#include "Utility/GMASSyncedEvent.h"
#include "GMCAbilityComponent.generated.h"


class UNiagaraComponent;
struct FFXSystemSpawnParameters;
class UNiagaraSystem;
class UGMCAbilityAnimInstance;
class UGMCAbilityMapData;
class UGMCAttributesData;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPreAttributeChanged, UGMCAttributeModifierContainer*, AttributeModifierContainer, UGMC_AbilitySystemComponent*,
                                             SourceAbilityComponent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnAttributeChanged, FGameplayTag, AttributeTag, float, OldValue, float, NewValue);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FGameplayAttributeChangedNative, const FGameplayTag&, const float, const float);
				
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAncillaryTick, float, DeltaTime);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSyncedEvent, const FGMASSyncedEventContainer&, EventData);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAbilityActivated, UGMCAbility*, Ability, FGameplayTag, AbilityTag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilityEnded, UGMCAbility*, Ability);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnActiveTagsChanged, FGameplayTagContainer, AddedTags, FGameplayTagContainer, RemovedTags);
DECLARE_MULTICAST_DELEGATE_TwoParams(FGameplayTagFilteredMulticastDelegate, const FGameplayTagContainer&, const FGameplayTagContainer&);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEffectApplied, UGMCAbilityEffect*, AppliedEffect);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEffectRemoved, UGMCAbilityEffect*, RemovedEffect);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTaskTimeout, FGameplayTag, TaskTag);

// Fires on the local autonomous proxy when replay occurrences cross the threshold
// configured in UGMASReplayBurstSettings. Counts frames-with-replay, not moves.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnReplayBurstDetected, int32, BurstCount, float, WindowSeconds);

USTRUCT()
struct FEffectStatePrediction
{
	GENERATED_BODY()

	FEffectStatePrediction(): EffectID(-1), State(-1){}

	UPROPERTY()
	int EffectID;

	UPROPERTY()
	uint8 State;
};

// Move-bound list of currently-active effect IDs. Replicated atomically with the
// move state, drives the Pending → Validated promotion on Predicted effects.
USTRUCT()
struct FGMASActiveEffectIDsState
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<int> IDs;
};

USTRUCT()
struct FGMASQueueOperationHandle
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Handle { -1 };

	UPROPERTY()
	int32 OperationId { -1 };

	UPROPERTY()
	int32 NetworkId { -1 };
};

// Snapshot of one active effect, sent server -> owning client on reconnect to
// rehydrate a freshly-constructed client ASC. Required because the standard
// per-effect apply RPC fires only at apply time; on a kept-pawn reconnect the
// new client's connection has no record of effects applied during the previous
// session, and ApplyStartingEffects on the server early-returns on its
// bStartingEffectsApplied flag.
//
// Slim by design: most config (PeriodicInterval, Modifiers, tag containers,
// chain rules) lives on the EffectClass CDO and is recovered locally via
// DuplicateObject. Only fields that can diverge at runtime are wired:
//   - EffectID    : preserved so EndEffect/cancel-by-id RPCs target the right instance
//   - TimeSinceStart : remap to client clock (preserves periodic boundaries + remaining duration)
//   - Duration    : runtime override of the CDO value
//   - bServerAuth : drives the client's tick-vs-passive decision
//   - EffectTag   : runtime override of the CDO value
USTRUCT()
struct FGMCEffectSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	TSubclassOf<UGMCAbilityEffect> EffectClass;

	UPROPERTY()
	int32 EffectID = 0;

	UPROPERTY()
	double TimeSinceStart = 0.0;

	UPROPERTY()
	double Duration = 0.0;

	UPROPERTY()
	bool bServerAuth = false;

	UPROPERTY()
	FGameplayTag EffectTag;
};

UENUM(BlueprintType)
enum class EGMCAbilityEffectQueueType : uint8
{
	/// Immediately applied, only valid within the GMC movement cycle. Should be applied on both client and server.
	Predicted UMETA(DisplayName="Predicted"),

	/// Predicted effect, not replicated but will be queued for addition in the next GMC movement cycle. Valid even
	/// outside of the GMC movement cycle. Should be applied on both client and server. If used during the GMC
	/// movement cycle, this is silently turned into Predicted.
	PredictedQueued UMETA(DisplayName="Predicted [Queued]"),

	/// Only valid on server; queued from server and sent to client via RPC. Valid even outside of the GMC movement cycle.
	ServerAuth UMETA(DisplayName="Server Auth"),

	/// Only valid on client; queued from client and sent to the server via GMC bindings. Valid even outside of the
	/// GMC movement cycle. You almost certainly don't want to use this, but it's here for the sake of completeness.
	ClientAuth UMETA(Hidden, DisplayName="Client Auth"),

	/// Only valid on server; queued from server and recorded in the GMC move history. Valid even outside of the GMC
	/// movement cycle. Slower than ServerAuth, only use this if you really need to preserve the effect application in
	/// the movement history. you almost certainly don't want to use this, but it's here for the sake of completeness.
	ServerAuthMove UMETA(Hidden, DisplayName="ADVANCED: Server Auth [Movement Cycle]")
};

UENUM(BlueprintType)
enum class EGMCEffectAnswerState : uint8
{
	// Effect is not answered yet
	Pending UMETA(DisplayName="Pending"),
	// Effect reach out of prediction windows, was cancelled but can be re-applied
	Timeout UMETA(DisplayName="Timeout"),
	// Effect was answered and applied
	Validated UMETA(DisplayName="Accepted")
};

class UGMCAbility;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="GMC Ability System Component"), meta=(Categories="GMAS"))
class GMCABILITYSYSTEM_API UGMC_AbilitySystemComponent : public UGameplayTasksComponent //  : public UGMC_MovementUtilityCmp
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UGMC_AbilitySystemComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// Client-auth EffectID namespace boundary. Effects applied via the ClientAuth
	// queue type are allocated in the [ClientAuthEffectIDOffset, INT32_MAX[ range
	// to avoid collision with Predicted / ServerAuth IDs (which are derived from
	// ActionTimer * 100 and stay in [1, ClientAuthEffectIDOffset[).
	//
	// Capacity:
	//   - Standard range : up to ~124 days of continuous ActionTimer
	//   - Client-auth    : ~1B unique IDs per session
	static constexpr int32 ClientAuthEffectIDOffset = 0x40000000;

	// Will apply the starting effects and abilities to the component,
	// bForce will re-apply the effects, usefull if we want to re-apply the effects after a reset (like a death)
	// Must be called on the server only
	virtual void ApplyStartingEffects(bool bForce = false);

	// Reconnection rehydration. The owning client requests a snapshot of every
	// active effect the server is tracking; the server replies with a slim wire
	// format and the client recreates local UGMCAbilityEffect instances bypassing
	// the apply pipeline (no bUniqueByEffectTag check, server EffectIDs preserved).
	// Triggered automatically from BeginPlay on the autonomous-proxy side.
	//
	// Fixes the kept-pawn reconnect drift: AGameModeMaster::PostLogin_ReconnectingPlayer
	// reuses the existing pawn (and its ASC), so bStartingEffectsApplied stays true and
	// no per-effect RPC is broadcast to the brand-new owning connection. Without this,
	// the new client never instantiates Recovery / Bleed / etc. locally and all bound
	// attributes drift permanently.
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RequestActiveEffectsSnapshot();

	UFUNCTION(Client, Reliable)
	void Client_ReceiveActiveEffectsSnapshot(const TArray<FGMCEffectSnapshot>& Snapshots);

	// Server-only: walk ActiveEffects and emit one snapshot entry per live effect.
	void BuildActiveEffectsSnapshot(TArray<FGMCEffectSnapshot>& OutSnapshots) const;

	// Client-only: recreate a UGMCAbilityEffect locally from a snapshot. Idempotent
	// (no-op if EffectID already in ActiveEffects) and tag-uniqueness-bypass.
	void RestoreEffectFromSnapshot(const FGMCEffectSnapshot& Snapshot);

	// Bound/Synced over GMC
	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	double ActionTimer;

	// Is this a server-only pawn (not player-controlled)?
	bool IsServerOnly() const;

	// Draw to log the attribute
	void DrawDebugAttribute(const FGameplayTag& AttributeTag) const;
	
	// Ability tags that the controller has 
	FGameplayTagContainer GetGrantedAbilities() const { return GrantedAbilityTags; }

	// Returns the union of bound (ActiveTags) and client-auth (ClientAuthActiveTags) tags.
	// Concatenation cost is O(N+M) per call; not cached. For introspection limited to
	// one source, see GetBoundActiveTags() / GetClientAuthActiveTags().
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Tags")
	FGameplayTagContainer GetActiveTags() const;

	// Bound-only view (validated server-side via GMC). Excludes client-auth tags.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Tags")
	const FGameplayTagContainer& GetBoundActiveTags() const { return ActiveTags; }

	// Client-auth view (locally maintained, eventually consistent). Excludes bound tags.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Tags")
	const FGameplayTagContainer& GetClientAuthActiveTags() const { return ClientAuthActiveTags; }

	// Return the active ability effects
	TMap<int, UGMCAbilityEffect*> GetActiveEffects() const { return ActiveEffects; }

	UGMCAbilityEffect* GetActiveEffectByHandle(int EffectID) const;

	// Return active Effect with tag
	// Match exact doesn't look for depth in the tag, it will only match the exact tag
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Abilities")
	TArray<UGMCAbilityEffect*> GetActiveEffectsByTag(const FGameplayTag& GameplayTag, bool bMatchExact = true) const;

	// Get the first active effect with the Effecttag
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Abilities")
	UGMCAbilityEffect* GetFirstActiveEffectByTag(const FGameplayTag& GameplayTag) const;

	// Return ability map that contains mapping of ability input tags to ability classes
	TMap<FGameplayTag, FAbilityMapData> GetAbilityMap() { return AbilityMap; }
	
	UFUNCTION(BlueprintCallable, Category="GMAS|Abilities")
	void AddAbilityMapData(UGMCAbilityMapData* AbilityMapData);

	// C++ overload: register a single entry directly without a data-asset wrapper.
	// Useful for test code and runtime ability grants.
	void AddAbilityMapData(const FAbilityMapData& AbilityMapData);

	UFUNCTION(BlueprintCallable, Category="GMAS|Abilities")
	void RemoveAbilityMapData(UGMCAbilityMapData* AbilityMapData);

	UFUNCTION(BlueprintCallable, Category="GMAS|Abilities")
	void AddStartingEffects(TArray<TSubclassOf<UGMCAbilityEffect>> EffectsToAdd);

	UFUNCTION(BlueprintCallable, Category="GMAS|Abilities")
	void RemoveStartingEffects(TArray<TSubclassOf<UGMCAbilityEffect>> EffectsToRemove);

	// Add an ability to the GrantedAbilities array
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	void GrantAbilityByTag(const FGameplayTag AbilityTag);

	// Remove an ability from the GrantedAbilities array
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	void RemoveGrantedAbilityByTag(const FGameplayTag AbilityTag);

	UFUNCTION(BlueprintPure, meta=(Categories="Ability"), Category = "GMCAbilitySystem")
	bool HasGrantedAbilityTag(const FGameplayTag GameplayTag) const;

	
	/**
	 * Add an active tag to the array
	 * @param AbilityTag Tag
	 * @warning Do not call this directly unless you exactly know what you are doing, that will cause desynchronization.
	* * @warning Do not call this directly unless you exactly know what you are doing, that will cause desynchronization.
	 * Prefer AddActiveSynchronizedTag
	 */
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	void AddActiveTag(const FGameplayTag AbilityTag);

	/**
	 * Remove an active tag to the array
	 * @param AbilityTag Tag
	 * @warning Do not call this directly unless you exactly know what you are doing, that will cause desynchronization.
	 * Prefer RemoveActiveSynchronizedTag
	 */
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	void RemoveActiveTag(const FGameplayTag AbilityTag);

	// Add a tag to the non-bound ClientAuthActiveTags container. Used by ClientAuth effects
	// to grant tags without triggering GMC validation divergence. Caller is responsible for
	// symmetric removal via RemoveClientAuthActiveTag.
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	void AddClientAuthActiveTag(const FGameplayTag Tag);

	// Symmetric removal for AddClientAuthActiveTag.
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	void RemoveClientAuthActiveTag(const FGameplayTag Tag);


	/**
	 * Will add a tag to the ActiveTag array on the server, and will replicate it nicely to the client
	 * @param Tag Tag to add
	 * @param AllowMultipleInstance if true, each application is reminded, and require same number of call of RemoveSynchronizedTag
	 * Authority Only, effect is used under the hood, however, DO NOT use this to add an effect, DO NOT use it with a tag already
	 * applied by an effect
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "GMCAbilitySystem")
	void AddSynchronizedTag(const FGameplayTag& Tag, bool AllowMultipleInstance = true);

	/**
	 * Will remove a tag to the ActiveTag array on the server, and will replicate it nicely to the client
	 * @param Tag Tag to remove
	 * @param RemoveEveryInstance if true, it will remove the tag without taking in consideration the number of application
	 * @warning Authority Only, effect is used under the hood, however, DO NOT use this to remove an effect, DO NOT use it with a tag already
	 * applied by an effect.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "GMCAbilitySystem")
	void RemoveSynchronizedTag(const FGameplayTag& Tag, bool RemoveEveryInstance = false);

	// Checks whether any active tag matches this tag or any of its children.
	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	bool HasActiveTag(const FGameplayTag GameplayTag) const;

	// Checks whether any active tag matches this tag exactly; it will not match on child tags.
	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	bool HasActiveTagExact(const FGameplayTag GameplayTag) const;

	// Checks whether any active tag matches any of the tags provided (or their children).
	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	bool HasAnyTag(const FGameplayTagContainer TagsToCheck) const;

	// Checks whether any active tag matches any of the tags provided exactly (excluding children).
	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	bool HasAnyTagExact(const FGameplayTagContainer TagsToCheck) const;

	// Checks whether every tag provided is in current tags, allowing for child tags.
	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	bool HasAllTags(const FGameplayTagContainer TagsToCheck) const;

	// Checks whether every tag provided is in current tags, without matching on child tags.
	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	bool HasAllTagsExact(const FGameplayTagContainer TagsToCheck) const;
	
	/** Get all active tags that match a given parent tag */
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	TArray<FGameplayTag> GetActiveTagsByParentTag(const FGameplayTag ParentTag);

	// ---- Bound-only query variants ---------------------------------------------------
	// These bypass ClientAuthActiveTags and inspect only the GMC-bound state. Use when
	// you specifically need the server-validated tag set (debug overlays, anti-cheat
	// checks, internal GMC bookkeeping). Default callers should prefer the union helpers.

	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	bool HasBoundActiveTag(const FGameplayTag GameplayTag) const;

	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	bool HasBoundActiveTagExact(const FGameplayTag GameplayTag) const;

	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	bool HasAnyBoundTag(const FGameplayTagContainer TagsToCheck) const;

	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	bool HasAnyBoundTagExact(const FGameplayTagContainer TagsToCheck) const;

	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	bool HasAllBoundTags(const FGameplayTagContainer TagsToCheck) const;

	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	bool HasAllBoundTagsExact(const FGameplayTagContainer TagsToCheck) const;

	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	TArray<FGameplayTag> GetBoundActiveTagsByParentTag(const FGameplayTag ParentTag);

	// Whitelist queries — public so tests, Blueprint, and external systems can introspect.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Client-Auth")
	bool IsClientAuthorizedAbility(TSubclassOf<UGMCAbility> AbilityClass) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Client-Auth")
	bool IsClientAuthorizedEffect(TSubclassOf<UGMCAbilityEffect> EffectClass) const;

	// Abilities that the owning client is authoritatively allowed to activate without
	// server-side ActivationRequiredTags / ActivationBlockedTags validation. The server
	// still honors BlockedByOtherAbility and Cooldown for runtime coherence.
	//
	// All entries MUST have bActivateOnMovementTick = false (validated at runtime via
	// ensure(); replay would be incoherent otherwise).
	UPROPERTY(EditDefaultsOnly, Category="GMAS|Client-Auth", meta=(DisplayName="Client-Authorized Abilities"))
	TArray<TSubclassOf<UGMCAbility>> ClientAuthorizedAbilities;

	// Effect classes that the owning client is authoritatively allowed to apply via
	// EGMCAbilityEffectQueueType::ClientAuth. The server still honors MustMaintainQuery
	// and MustHaveTags / MustNotHaveTags during the effect's lifetime.
	UPROPERTY(EditDefaultsOnly, Category="GMAS|Client-Auth", meta=(DisplayName="Client-Authorized Effects"))
	TArray<TSubclassOf<UGMCAbilityEffect>> ClientAuthorizedAbilityEffects;

	// Do not call directly on client, go through QueueAbility
	bool TryActivateAbilitiesByInputTag(const FGameplayTag& InputTag, const UInputAction* InputAction = nullptr, const bool bFromMovementTick=true, const bool bForce=false);
	
	// Do not call directly on client, go through QueueAbility. Can be used to call server-side abilities (like AI).
	// bSkipActivationTagsCheck=true bypasses CheckActivationTags(CDO). Used by the
	// client-auth path; default false preserves all existing call sites.
	bool TryActivateAbility(TSubclassOf<UGMCAbility> ActivatedAbility,
	                        const UInputAction* InputAction = nullptr,
	                        const FGameplayTag ActivationTag = FGameplayTag::EmptyTag,
	                        bool bSkipActivationTagsCheck = false);


	/**
	 * Queue an ability for activation based on the provided input tag and action.
	 *
	 * @param InputTag              The gameplay tag associated with the ability to queue.
	 * @param InputAction           The input action triggering the ability.
	 * @param bPreventConcurrentActivation If true, prevents the concurrent activation of abilities already in progress. The check is made locally
	 * 							   reducing server charge, but also required in case of activation key wait inside the ability.
	 */
	UFUNCTION(BlueprintCallable, DisplayName="Activate Ability", Category="GMAS|Abilities")
	void QueueAbility(UPARAM(meta=(Categories="Input"))
	                  FGameplayTag InputTag, const UInputAction* InputAction = nullptr, bool bPreventConcurrentActivation = false);

	UFUNCTION(BlueprintCallable, DisplayName="Count Queued Ability Instances", Category="GMAS|Abilities")
	int32 GetQueuedAbilityCount(FGameplayTag AbilityTag);

	UFUNCTION(BlueprintCallable, DisplayName="Count Activated Ability Instances", Category="GMAS|Abilities")
	int32 GetActiveAbilityCount(TSubclassOf<UGMCAbility> AbilityClass);

	// Perform a check in every active ability against BlockOtherAbility and check if the tag provided is present
	virtual bool IsAbilityTagBlocked(const FGameplayTag AbilityTag) const;

	UFUNCTION(BlueprintCallable, DisplayName="End Abilities (By Tag)", Category="GMAS|Abilities")
	// End all abilities with the corresponding tag, returns the number of abilities ended
	int EndAbilitiesByTag(FGameplayTag AbilityTag);

	UFUNCTION(BlueprintCallable, DisplayName="End Abilities (By Class)", Category="GMAS|Abilities")
	// End all abilities with the corresponding tag, returns the number of abilities ended
	int EndAbilitiesByClass(TSubclassOf<UGMCAbility> AbilityClass);
	
	UFUNCTION(BlueprintCallable, DisplayName = "End Abilities (By Definition Query)", Category="GMAS|Abilities")
	// End all abilities matching query
	int EndAbilitiesByQuery(const FGameplayTagQuery& Query);

	UFUNCTION(BlueprintCallable, DisplayName="Count Activated Ability Instances (by tag)", Category="GMAS|Abilities")
	int32 GetActiveAbilityCountByTag(FGameplayTag AbilityTag);
	
	void QueueTaskData(const FInstancedStruct& TaskData);

	// Set an ability cooldown
	// If it's already on cooldown, subsequent calls will overwrite it
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	void SetCooldownForAbility(const FGameplayTag AbilityTag, float CooldownTime);

	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	float GetCooldownForAbility(const FGameplayTag AbilityTag) const;
	
	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	float GetMaxCooldownForAbility(TSubclassOf<UGMCAbility> Ability) const;

	// Get the cooldowns for all abilities associated with Input tag
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	TMap<FGameplayTag, float> GetCooldownsForInputTag(const FGameplayTag InputTag);
	/**
	 * Will add/remove a given gameplay tag to the ASC based on the bool inputted.
	 * Call this function on Prediction Tick.
	 * A good example of this is something like a State.InAir tag.
	 */
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	void MatchTagToBool(const FGameplayTag& InTag, bool MatchedBool);

	// A UGMCAttributesData asset that defines the default attributes for this component
	UPROPERTY(EditDefaultsOnly, DisplayName="Attributes", Category = "GMCAbilitySystem")
	TArray<UGMCAttributesData*> AttributeDataAssets; 

	/** Struct containing attributes that are bound to the GMC */
	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	FGMCAttributeSet BoundAttributes;

	/** Reminder to check attributes */
	UPROPERTY()
	FGMCAttributeSet OldBoundAttributes;

	/** Struct containing attributes that are replicated and unbound from the GMC */
	UPROPERTY(ReplicatedUsing = OnRep_UnBoundAttributes, BlueprintReadOnly, Category = "GMCAbilitySystem")
	FGMCUnboundAttributeSet UnBoundAttributes;

	UPROPERTY()
	FGMCUnboundAttributeSet OldUnBoundAttributes;
	
	UFUNCTION()
	void OnRep_UnBoundAttributes();

	void CheckUnBoundAttributeChanged();

	int GetNextAvailableEffectID() const;
	// Allocate an EffectID in the client-auth reserved range. Returns -1 if
	// ActionTimer is zero (uninitialized component / smoothed listen-server pawn).
	int GetNextAvailableClientAuthEffectID() const;
	bool CheckIfEffectIDQueued(int EffectID) const;
	// int CreateEffectOperation(TGMASBoundQueueOperation<UGMCAbilityEffect, FGMCAbilityEffectData>& OutOperation, const TSubclassOf<UGMCAbilityEffect>& Effect, const FGMCAbilityEffectData& EffectData, bool bForcedEffectId = true, EGMCAbilityEffectQueueType QueueType = EGMCAbilityEffectQueueType::Predicted);
	// int CreateSyncedEventOperation(TGMASBoundQueueOperation<UGMASSyncedEvent, FGMASSyncedEventContainer>& OutOperation, const FGMASSyncedEventContainer& EventData);
	
	// BP-specific version of 
	
	/**
	 * Applies an effect to the ability component. If the Queue Type is Predicted, the effect will be immediately added
	 * on both client and server; this must happen within the GMC movement lifecycle for it to be valid. If the
	 * Queue Type is anything else, the effect must be queued on the server and will be replicated to the client.
	 * @param HandlingAbility : Optional ability handling, if provided, the end ability will trigger also automatically the end of the effect.
	 */
	// OutEffectHandle is a deprecated alias of OutEffectId, kept for binary/BP compatibility.
	// All apply paths now mirror the EffectId into the handle. New code should read OutEffectId
	// and use the *ById remove APIs instead of *ByHandle (which are themselves deprecated).
	UFUNCTION(BlueprintCallable, Category="GMAS|Effects", DisplayName="Apply Ability Effect")
	void ApplyAbilityEffectSafe(TSubclassOf<UGMCAbilityEffect> EffectClass, FGMCAbilityEffectData InitializationData, EGMCAbilityEffectQueueType QueueType,
		UPARAM(DisplayName="Success")
		bool& OutSuccess,
		UPARAM(DisplayName="Effect Handle (Deprecated — alias of Network ID)") int& OutEffectHandle,
		UPARAM(DisplayName="Effect Network ID") int& OutEffectId,
		UPARAM(DisplayName="Effect Instance") UGMCAbilityEffect*& OutEffect,
		UPARAM(DisplayName="(Opt) Ability Handling") UGMCAbility* HandlingAbility = nullptr);

	/** Short version of ApplyAbilityEffect (Fire and Forget, return nullptr if fail, or the effect instance if success)
	 * Don't suggest it for BP user to avoid confusion.
	 *  @param HandlingAbility : Optional ability handling, if provided, the end ability will trigger al
	 */
	UGMCAbilityEffect* ApplyAbilityEffectShort(TSubclassOf<UGMCAbilityEffect> EffectClass, EGMCAbilityEffectQueueType QueueType, 
		UPARAM(DisplayName="(Opt) Ability Handling") UGMCAbility* HandlingAbility = nullptr);

	/**
	 * Applies an effect to the ability component. If the Queue Type is Predicted, the effect will be immediately added
	 * on both client and server; this must happen within the GMC movement lifecycle for it to be valid. If the
	 * Queue Type is anything else, the effect must be queued on the server and will be replicated to the client.
	 * 
	 * @param EffectClass The class of ability effect to add.
	 * @param InitializationData The initialization data for the ability effect.
	 * @param QueueType How to queue the effect.
	 * @param OutEffectHandle Deprecated alias of OutEffectId, kept for compatibility. Mirrored from OutEffectId on every apply path.
	 * @param OutEffectId The newly-created effect's network ID, if one is available. Valid across server/client.
	 * @param OutEffect The newly-created effect instance, if available.
	 * @return true if the effect was applied, false otherwise.
	 */
	bool ApplyAbilityEffect(TSubclassOf<UGMCAbilityEffect> EffectClass, FGMCAbilityEffectData InitializationData, EGMCAbilityEffectQueueType QueueType, int& OutEffectHandle, int& OutEffectId, UGMCAbilityEffect*& OutEffect);

	UGMCAbilityEffect* ApplyAbilityEffectViaOperation(const FGMASBoundQueueV2ApplyEffectOperation& Operatio);
	
	// Do not call this directly unless you know what you are doing. Otherwise, always go through the above ApplyAbilityEffect variant!
	UGMCAbilityEffect* ApplyAbilityEffect(UGMCAbilityEffect* Effect, FGMCAbilityEffectData InitializationData);
	
	UFUNCTION(BlueprintCallable, Category="GMAS|Effects")
	UGMCAbilityEffect* GetEffectById(const int EffectId) const;

	UFUNCTION(BlueprintCallable, Category="GMAS|Effects")
	TArray<UGMCAbilityEffect*> GetEffectsByIds(const TArray<int> EffectIds) const;

	UFUNCTION(BlueprintCallable, Category="GMAS|Effects")
	FString GetEffectsNameAsString(const TArray<UGMCAbilityEffect*>& EffectList) const;

	TArray<int> EffectsMatchingTag(const FGameplayTag& Tag, int32 NumToRemove = -1) const;

	// Do not call this directly unless you know what you are doing; go through the RemoveActiveAbilityEffectSafe if
	// doing this from outside of the component, to allow queuing and sanity-check.
	UFUNCTION(BlueprintCallable, Category="GMAS|Effects")
	void RemoveActiveAbilityEffect(UGMCAbilityEffect* Effect);

	// The "handle" is a deprecated alias of the EffectId; this API simply forwards to the
	// id-based path. Prefer RemoveActiveAbilityEffectSafe (by pointer) or RemoveEffectByIdSafe
	// (by id) — they're the load-bearing variants and won't go away.
	UE_DEPRECATED(5.7, "EffectHandle is now an alias of EffectId; use RemoveEffectByIdSafe with the EffectId returned from ApplyAbilityEffectSafe.")
	UFUNCTION(BlueprintCallable, Category="GMAS|Effects", meta=(DeprecatedFunction, DeprecationMessage="EffectHandle is an alias of EffectId. Use RemoveEffectByIdSafe with the EffectId returned by ApplyAbilityEffectSafe."))
	void RemoveActiveAbilityEffectByHandle(int EffectHandle, EGMCAbilityEffectQueueType QueueType = EGMCAbilityEffectQueueType::Predicted);

	UFUNCTION(BlueprintCallable, Category="GMAS|Effects", DisplayName="Remove Active Ability Effect (Safe)")
	void RemoveActiveAbilityEffectSafe(UGMCAbilityEffect* Effect, EGMCAbilityEffectQueueType QueueType = EGMCAbilityEffectQueueType::Predicted);
	
	UFUNCTION(BlueprintCallable, Category="GMAS|Effects", DisplayName="Remove Active Ability Effect by Tag (Safe)")
	void RemoveActiveAbilityEffectByTag(const FGameplayTag& Tag, EGMCAbilityEffectQueueType QueueType = EGMCAbilityEffectQueueType::Predicted, bool bAllInstance = false);

	/**
	 * Removes an instanced effect if it exists. If NumToRemove == -1, remove all. Returns the number of removed instances.
	 * If the inputted count is higher than the number of active corresponding effects, remove all we can.
	 */
	UFUNCTION(BlueprintCallable, Category="GMAS|Effects", DisplayName="Remove Effect by Tag (Legacy)", meta=(DeprecatedFunction, DeprecationMessage="Please use the more modern RemoveEffectByTagSafe which takes a queue type."))
	int32 RemoveEffectByTag(FGameplayTag InEffectTag, int32 NumToRemove=-1, bool bOuterActivation = false);

	/**
	 * Removes an instanced effect if it exists. If NumToRemove == -1, remove all. Returns the number of removed instances.
	 * If the inputted count is higher than the number of active corresponding effects, remove all we can.
	 */
	UFUNCTION(BlueprintCallable, Category="GMAS|Effects", DisplayName="Remove Effects by Tag (Safe)")
	int32 RemoveEffectByTagSafe(FGameplayTag InEffectTag, int32 NumToRemove=-1, EGMCAbilityEffectQueueType QueueType = EGMCAbilityEffectQueueType::Predicted);
	
	/**
	 * Removes an instanced effect by ids.
	 * return false if any of the ids are invalid.
	 */
	UFUNCTION(BlueprintCallable, Category="GMAS|Effects", DisplayName="Remove Effects by Id (Legacy)", meta=(DeprecatedFunction, DeprecationMessage="Please use the more modern RemoveEffectByIdSafe which takes a queue type."))
	bool RemoveEffectById(TArray<int> Ids, bool bOuterActivation = false);

	/**
	 * Removes an instanced effect by ids.
	 * return false if any of the ids are invalid.
	 */
	UFUNCTION(BlueprintCallable, Category="GMAS|Effects", DisplayName="Remove Effects by Id (Safe)")
	bool RemoveEffectByIdSafe(TArray<int> Ids, EGMCAbilityEffectQueueType QueueType = EGMCAbilityEffectQueueType::Predicted);

	// The "handle" is a deprecated alias of the EffectId; this API just unwraps the id and
	// forwards to RemoveEffectByIdSafe. New code should call RemoveEffectByIdSafe directly with
	// the EffectId returned by ApplyAbilityEffectSafe.
	UE_DEPRECATED(5.7, "EffectHandle is now an alias of EffectId; use RemoveEffectByIdSafe with the EffectId returned from ApplyAbilityEffectSafe.")
	UFUNCTION(BlueprintCallable, Category="GMAS|Effects", DisplayName="Remove Effect by Handle", meta=(DeprecatedFunction, DeprecationMessage="EffectHandle is an alias of EffectId. Use RemoveEffectByIdSafe with the EffectId returned by ApplyAbilityEffectSafe."))
	bool RemoveEffectByHandle(int EffectHandle, EGMCAbilityEffectQueueType QueueType);
	
	UFUNCTION(BlueprintCallable, Category="GMAS|Effects", DisplayName="Remove Effects by Definition Query")
	int32 RemoveEffectsByQuery(const FGameplayTagQuery& Query, EGMCAbilityEffectQueueType QueueType);

	/**
	 * Gets the number of active effects with the inputted tag.
	 * Returns -1 if tag is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category="GMAS|Effects")
	int32 GetNumEffectByTag(FGameplayTag InEffectTag);

	//// Event Delegates
	// Called before an attribute is about to be changed
	UPROPERTY(BlueprintAssignable)
	FOnPreAttributeChanged OnPreAttributeChanged;

	// Called after an attribute has been changed
	UPROPERTY(BlueprintAssignable)
	FOnAttributeChanged OnAttributeChanged;

	// Called during the Ancillary Tick
	UPROPERTY(BlueprintAssignable)
	FOnAncillaryTick OnAncillaryTick;

	// Called when a task times out
	UPROPERTY(BlueprintAssignable)
	FOnTaskTimeout OnTaskTimeout;
	
	////

	// Called when the set of active tags changes.
	UPROPERTY(BlueprintAssignable)
	FOnActiveTagsChanged OnActiveTagsChanged;

	// Called when an ability is successfully activated
	UPROPERTY(BlueprintAssignable)
	FOnAbilityActivated OnAbilityActivated;

	UPROPERTY(BlueprintAssignable)
	FOnAbilityEnded OnAbilityEnded;

	// Called when a synced event is executed
	UPROPERTY(BlueprintAssignable)
	FOnSyncedEvent OnSyncedEvent;

	FGameplayTagContainer PreviousActiveTags;

	UPROPERTY(BlueprintAssignable)
	FOnEffectApplied OnEffectApplied;

	UPROPERTY(BlueprintAssignable)
	FOnEffectRemoved OnEffectRemoved;

	// Local autonomous proxy only. Reset-on-fire — sustained chain refires ~once per WindowSeconds.
	UPROPERTY(BlueprintAssignable, Category="GMAS|Diagnostics")
	FOnReplayBurstDetected OnReplayBurstDetected;

	/** Returns an array of pointers to all attributes */
	TArray<const FAttribute*> GetAllAttributes() const;

	/** Get an Attribute using its Tag */
	const FAttribute* GetAttributeByTag(UPARAM(meta=(Categories="Attribute")) FGameplayTag AttributeTag) const;

	TMap<int, UGMCAbility*> GetActiveAbilities() const { return ActiveAbilities; }

	// Get Attribute value (RawValue + Temporal Modifiers) by Tag
	// This value is replicated on simulated proxy !
	UFUNCTION(BlueprintPure, Category="GMAS|Attributes")
	float GetAttributeValueByTag(UPARAM(meta=(Categories="Attribute"))FGameplayTag AttributeTag) const;

	// Get Attribute Value without Temporal Modifiers
	// his value is replicated on simulated proxy !
	UFUNCTION(BlueprintPure, Category="GMAS|Attributes")
	float GetAttributeRawValue(UPARAM(meta=(Categories="Attribute"))FGameplayTag AttributeTag) const;

	// Get Attribute value by Tag
	UFUNCTION(BlueprintPure, Category="GMAS|Attributes")
	FAttributeClamp GetAttributeClampByTag(UPARAM(meta=(Categories="Attribute"))FGameplayTag AttributeTag) const;
	
	// Set Attribute value by Tag
	// Will NOT trigger an "OnAttributeChanged" Event
	// bResetModifiers: Will reset all modifiers on the attribute to the base value. DO NOT USE if you have any active effects that modify this attribute.
	UFUNCTION(BlueprintCallable, Category="GMAS|Attributes", meta=(DeprecatedFunction, DeprecationMessage="Please use ApplyAbilityAttributeModifier instead."))
	bool SetAttributeValueByTag(UPARAM(meta=(Categories="Attribute"))FGameplayTag AttributeTag, float NewValue, bool bResetModifiers = false);
	
	/** Get the default value of an attribute from the data assets. */
	UFUNCTION(BlueprintCallable, Category="GMAS|Attributes")
	float GetAttributeInitialValueByTag(UPARAM(meta=(Categories="Attribute"))FGameplayTag AttributeTag) const;
	
	// Apply modifiers that affect attributes
	UFUNCTION(BlueprintCallable, Category="GMAS|Attributes")
	void ApplyAbilityAttributeModifier(const FGMCAttributeModifier& AttributeModifier);

	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	bool bJustTeleported;

	UFUNCTION(BlueprintCallable, Category="GMAS")
	bool HasAuthority() const { return GetOwnerRole() == ROLE_Authority; }

	// True while we're inside the AncillaryTick scope (set at the top of GenAncillaryTick,
	// cleared at the bottom). Used by callers that need to distinguish "in any GMC tick
	// context" vs "outside any tick" to pick the right effect-removal path.
	bool IsInAncillaryTick() const { return bInAncillaryTick; }
	
	UPROPERTY(BlueprintReadWrite, AdvancedDisplay, Category = "GMCAbilitySystem")
	UGMC_MovementUtilityCmp* GMCMovementComponent;

	UFUNCTION(Server, Reliable)
	void RPCTaskHeartbeat(int AbilityID, int TaskID);

	/**
	 * Adds a filtered delegate to be called if any tag matching the filter is added or removed. Tag matching is not
	 * exact, so parent tags can be provided.
	 * @param Tags A list of tags we care about.
	 * @param Delegate A delegate to call if a tag matching the filter is added or removed.
	 * @return A delegate handle, suitable for passing to RemoveFilteredTagChangeDelegate.
	 */
	FDelegateHandle AddFilteredTagChangeDelegate(const FGameplayTagContainer& Tags, const FGameplayTagFilteredMulticastDelegate::FDelegate& Delegate);

	/**
	 * Removes a previously-added filtered delegate on tag changes.
	 * @param Tags A list of tags the delegate was bound to
	 * @param Handle The handle of the delegate to unbind
	 */
	void RemoveFilteredTagChangeDelegate(const FGameplayTagContainer& Tags, FDelegateHandle Handle);

	/**
	 * Adds a native (e.g. suitable for use in structs) delegate binding for attribute changes.
	 * @param Delegate The delegate to call on attribute changes.
	 * @return A handle to use when removing this delegate.
	 */
	FDelegateHandle AddAttributeChangeDelegate(const FGameplayAttributeChangedNative::FDelegate& Delegate);

	/**
	 * Removes a native (e.g. suitable for use in structs) delegate for attribute changes.
	 * @param Handle The delegate handle to be removed.
	 */
	void RemoveAttributeChangeDelegate(FDelegateHandle Handle);


#pragma region GMC
	// GMC
	UFUNCTION(BlueprintCallable, Category="GMAS")
	virtual void BindReplicationData();
	
	UFUNCTION(BlueprintCallable, Category="GMAS")
	virtual void GenAncillaryTick(float DeltaTime, bool bIsCombinedClientMove);
	
	UFUNCTION(BlueprintCallable, Category="GMAS")
	virtual void GenPredictionTick(float DeltaTime);

	UFUNCTION(BlueprintCallable, Category="GMAS")
	virtual void GenSimulationTick(float DeltaTime);

	UFUNCTION(BlueprintCallable, Category="GMAS")
	virtual void PreLocalMoveExecution();
	
	
#pragma endregion GMC

#pragma region ToStringHelpers
	/** Get all attributes in string format. Used in the gameplay debugger. */
	FString GetAllAttributesString() const;

	/** Get all active effect data in string format. Used in the gameplay debugger. */
	FString GetActiveEffectsDataString() const;

	/** Get all active effects in string format. Used in the gameplay debugger. */
	FString GetActiveEffectsString() const;

	/** Get all active abilities in string format. Used in the gameplay debugger. */
	FString GetActiveAbilitiesString() const;

#pragma endregion ToStringHelpers
	
protected:

	//TODO: Move these functions

	UFUNCTION(Client, Reliable)
	void RPCOnServerOperationAdded(int OperationID, const FInstancedStruct Operation);

	UFUNCTION(BlueprintCallable, Category="GMAS")
	void BoundQueueV2Debug(TSubclassOf<UGMCAbilityEffect> Effect);

	UFUNCTION()
	void OnServerOperationForced(FInstancedStruct OperationData);
	
	virtual void BeginPlay() override;
	
	// Abilities that are granted to the player (bound)
	FGameplayTagContainer GrantedAbilityTags;

	// return true if the ability is allowed to be activated considering active tags
	virtual bool CheckActivationTags(const UGMCAbility* Ability) const;

	// Reduced activation check used only by the client-auth path. Skips
	// ActivationRequiredTags, ActivationBlockedTags, and ActivationQuery
	// (those are user-permission gates the client is allowed to bypass).
	// Still honors BlockedByOtherAbility, IsAbilityTagBlocked, and active
	// cooldowns for runtime coherence of the ASC state machine.
	virtual bool CheckActivationTagsForClientAuth(const UGMCAbility* Ability) const;

	// Effect tags that are granted to the player (bound)
	FGameplayTagContainer ActiveTags;

	// Tags posed by ClientAuth effects. NOT bound to GMC -- never validated server-side.
	// Eventually consistent: server populates its copy when it processes the
	// ClientAuthEffectOperation; client populates immediately at apply time.
	// Queried in union with ActiveTags by all HasActiveTag* helpers.
	UPROPERTY()
	FGameplayTagContainer ClientAuthActiveTags;

	UPROPERTY(EditDefaultsOnly, Category="Ability")
	FGameplayTagContainer StartingAbilities;

	UPROPERTY(EditDefaultsOnly, Category="Ability")
	TArray<TSubclassOf<UGMCAbilityEffect>> StartingEffects;

	UPROPERTY(EditDefaultsOnly, Category="Tags")
	FGameplayTagContainer StartingTags;

	// Returns the matching abilities in the AbilityMap if they have been granted
	TArray<TSubclassOf<UGMCAbility>> GetGrantedAbilitiesByTag(FGameplayTag AbilityTag);
	
	// Sync'd containers for abilities and effects
	FGMCAbilityData AbilityData;
	
	FInstancedStruct TaskData = FInstancedStruct::Make(FGMCAbilityTaskData{});;

	void ClearAbilityAndTaskData();

	void SendTaskDataToActiveAbility(bool bFromMovement);

private:

	bool bStartingEffectsApplied = false;

	// Reconnect-snapshot client-pull state. The Server RPC requires the actor to
	// have an established owning connection AND the local role to be AutonomousProxy
	// — neither is guaranteed at component-BeginPlay time on a freshly-replicated
	// pawn. We defer with a bounded retry instead of firing optimistically and
	// losing the request silently.
	bool bSnapshotRequestSent = false;
	int32 SnapshotRequestRetryCount = 0;
	// ~2s @ 60fps. Bound exists so simulated-proxy ASCs (other players' pawns
	// visible to us) eventually stop polling — they will never satisfy the
	// IsLocallyControlled check.
	static constexpr int32 MaxSnapshotRequestRetries = 120;

	// Owning-client gate + retry. Fires Server_RequestActiveEffectsSnapshot when
	// the pawn is locally controlled and ROLE_AutonomousProxy; otherwise schedules
	// itself on the next tick until either the gate opens or the retry budget
	// is exhausted.
	void TryRequestActiveEffectsSnapshot();

	// Activate a client-auth ability locally (and forward to server via BoundQueueV2 if
	// running on a non-authoritative client). Returns true if successfully activated.
	// Only called from QueueAbility -- direct invocation bypasses the whitelist gate.
	bool TryActivateClientAuthAbility(TSubclassOf<UGMCAbility> AbilityClass,
	                                  FGameplayTag InputTag,
	                                  const UInputAction* InputAction);

	// Array of data objects to initialize the component's ability map
	UPROPERTY(EditDefaultsOnly, Category="Ability")
	TArray<TObjectPtr<UGMCAbilityMapData>> AbilityMaps;
	
	// Map of Ability Tags to Ability Classes
	TMap<FGameplayTag, FAbilityMapData> AbilityMap;

public:
	// Empty the AbilityMap and remove all granted abilities from existing maps
	UFUNCTION(BlueprintCallable, Category="GMAS|Abilities")
	void ClearAbilityMap();
	

	virtual void SetAttributeInitialValue(const FGameplayTag& AttributeTag, float& BaseValue);

	UFUNCTION(BlueprintImplementableEvent, Category="GMAS|Abilities")
	void OnInitializeAttributeInitialValue(const FGameplayTag& AttributeTag, float& BaseValue);

	// Will calculate and process stack of attributes.
	// Pass bInGenPredictionTick=true to process GMC-bound attributes (prediction path),
	// false for the ancillary path.  Called internally by Gen*Tick; also exposed here
	// so tests can drive recalculation with a manually controlled ActionTimer.
	void ProcessAttributes(bool bInGenPredictionTick);

	// Tick predicted and active effects (CheckState + duration expiry + periodic ticks).
	// Called internally by Gen*Tick; also exposed here so tests can advance effect state
	// with a manually controlled ActionTimer without going through GenPredictionTick
	// (which overwrites ActionTimer from GMCMovementComponent->GetMoveTimestamp()).
	void TickActiveEffects(float DeltaTime);

private:
	// List of filtered tag delegates to call when tags change.
	TArray<TPair<FGameplayTagContainer, FGameplayTagFilteredMulticastDelegate>> FilteredTagDelegates;

	FGameplayAttributeChangedNative NativeAttributeChangeDelegate;
	
	TArray<FModifierHistoryEntry> ModifierHistory;
	
	// Get the map from the data asset and apply that to the component's map
	void InitializeAbilityMap();
	void RemoveAbilityMapData(const FAbilityMapData& AbilityMapData);

	// Add the starting ability tags to GrantedAbilities at start
	void InitializeStartingAbilities();
	
	TArray<FInstancedStruct> QueuedTaskData;
	
	FGMASBoundQueueV2 BoundQueueV2 = {};

	// Local buffer for PredictedQueued operations called outside of a movement tick.
	// Both client and server maintain this independently — no replication needed.
	TArray<FInstancedStruct> PendingPredictedOperations;

	// Drains all buffered PredictedQueued operations. Called at the start of
	// GenPredictionTick and GenAncillaryTick.
	void DrainPendingPredictedOperations();

	// Events
	virtual bool ProcessOperation(FInstancedStruct OperationData, bool bFromMovementTick = true, bool bForce = false);
	virtual void ProcessEffectApplicationFromOperation(const FGMASBoundQueueV2ApplyEffectOperation& Data);

	virtual void ServerProcessOperation(const FInstancedStruct& OperationData, bool bFromMovementTick = true);
	virtual void ServerProcessAcknowledgedOperation(int OperationID, bool bFromMovementTick = true);

	// Event Implementations

	// Execute an event that is created by the server where execution is synced between server and client
	UFUNCTION(BlueprintCallable, Category = "GMASSyncedEvent")
	void ExecuteSyncedEvent(FGMASSyncedEventContainer EventData);
	
	UFUNCTION(BlueprintCallable, DisplayName="Add Impulse (Synced Event)", Category = "GMASSyncedEvent")
	void AddImpulse(FVector Impulse, bool bVelChange = false);

	UFUNCTION(BlueprintCallable, DisplayName="Set Actor Location (Synced Event)", Category = "GMASSyncedEvent")
	void SetActorLocation(FVector Location);
	
	UPROPERTY()
	TMap<int, UGMCAbility*> ActiveAbilities;
	
	UPROPERTY()
	TMap<FGameplayTag, float> ActiveCooldowns;

	int GenerateAbilityID() const {return ActionTimer * 100;}
	
	// Set Attributes to either a default object or a provided TSubClassOf<UGMCAttributeSet> in BP defaults
	// This must run before variable binding
	void InstantiateAttributes();

	

	void SetStartingTags();

	// Check if ActiveTags has changed and call delegates
	void CheckActiveTagsChanged();

	// Check if any Attribute has changed and call delegates
	void CheckAttributeChanged();

	void CheckAttributeChanged_Internal(FGMCAttributeSet& OldAttributeSet, FGMCAttributeSet& NewAttributeSet);
	
	// Clear out abilities in the Ended state from the ActivateAbilities map
	void CleanupStaleAbilities();

	// Tick active abilities, primarily the Tasks inside them
	void TickActiveAbilities(float DeltaTime);

	// Tick active abilities, but from the ancillary tick rather than prediction
	void TickAncillaryActiveAbilities(float DeltaTime);

	// Tick ability cooldowns
	void TickActiveCooldowns(float DeltaTime);

	// Max time a client will predict an effect without it being confirmed by the server before cancelling
	float ClientEffectApplicationTimeout = 1.f;

	UPROPERTY()
	TMap<int, UGMCAbilityEffect*> ActiveEffects;

	// GMC-bound list of currently-active EffectIDs. Authoritative source for the
	// Pending → Validated transition on Predicted effects. See FGMASActiveEffectIDsState
	// docstring for the full rationale.
	UPROPERTY()
	FInstancedStruct ActiveEffectIDsBound;

public:
	// Helpers to mutate / query the bound state without callers having to wrangle the
	// FInstancedStruct accessor + struct construction every time. Public so unit tests
	// and external systems (debug overlays, scripts) can inspect the state without
	// going through the bound-state machinery.
	void BoundActiveEffectIDs_Add(int EffectID);
	void BoundActiveEffectIDs_Remove(int EffectID);
	bool BoundActiveEffectIDs_Contains(int EffectID) const;

private:
	// IDs that have been claimed by server-auth effect applications
	TArray<int> ReservedEffectIDs;

	UPROPERTY()
	TMap<int, FGMASQueueOperationHandle> EffectHandles;

	int GetNextAvailableEffectHandle() const;

	UFUNCTION(BlueprintCallable, Category="GMAS|Effects")
	void GetEffectFromHandle_BP(int EffectHandle, bool& bOutSuccess, int32& OutEffectNetworkId, UGMCAbilityEffect*& OutEffect);
	
	bool GetEffectFromHandle(int EffectHandle, int32& OutEffectNetworkId, UGMCAbilityEffect*& OutEffect) const;
	bool GetEffectHandle(int EffectHandle, FGMASQueueOperationHandle& HandleData) const;

	void RemoveEffectHandle(int EffectHandle);
	
	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem", meta=(AllowPrivateAccess="true"))
	bool bInAncillaryTick = false;
	

	int LateApplicationIDCounter = 0;

	// Effect IDs that have been processed and don't need to be remade when ActiveEffectsData is replicated
	// This need to be persisted for a while
	// This never empties out so it'll infinitely grow, probably a better way to accomplish this
	UPROPERTY()
	TMap<int /*ID*/, EGMCEffectAnswerState /*bServerConfirmed*/> ProcessedEffectIDs;

	// Predict-replace recovery map (client-only). Key: SUCCESSOR effect's ID (the new
	// instance applied via bUniqueByEffectTag REPLACE). Value: list of OLD instances
	// that were marked bPendingDeathBySuccessor by that apply, awaiting server's verdict.
	// On Pending → Validated: finalize each entry (real EndEffect on the OLD).
	// On Pending → Timeout (server rejected): revive each entry (clear bPendingDeathBySuccessor).
	// Server never populates this — server's force-end runs immediately at apply time.
	//
	// Not UPROPERTY: UHT rejects TMap with TArray-typed values (nested-container limit).
	// TWeakObjectPtr handles its own GC staleness tracking so we don't need reflection
	// for safety — entries naturally expire if the effect UObject is GC'd.
	TMap<int /*SuccessorID*/, TArray<TWeakObjectPtr<UGMCAbilityEffect>>> PendingReplacements;

	// Let the client know that the server has activated this ability as well
	// Needed for the client to cancel mis-predicted abilities
	UFUNCTION(Client, Reliable)
	void RPCConfirmAbilityActivation(int AbilityID);

	// Let the client know that the server has ended an ability
	// In most cases, the client should have predicted this already,
	// this is just for redundancy
	UFUNCTION(Client, Reliable)
	void RPCClientEndAbility(int AbilityID);
	
	// Let the client know that the server has ended an effect
	// In most cases, the client should have predicted this already,
	// this is just for redundancy
	UFUNCTION(Client, Reliable)
	void RPCClientEndEffect(int EffectID);

	// Set in GenPredictionTick (re-entrant during replay), consumed once in GenAncillaryTick.
	bool bReplayObservedThisFrame = false;

	TArray<double> RecentReplayTimestamps;

	UPROPERTY(Transient)
	TWeakObjectPtr<class UUserWidget> ActiveReplayWarningWidget;

	FTimerHandle ReplayWarningWidgetTimerHandle;

	void ProcessReplayBurstDiagnostic();
	void TryShowReplayBurstWarningWidget();

	friend UGMCAbilityAnimInstance;

#if WITH_AUTOMATION_WORKER
public:
	// Test-only accessors — compiled away in non-editor/non-test builds.
	TMap<int, EGMCEffectAnswerState>&      GetProcessedEffectIDsForTest()  { return ProcessedEffectIDs; }
	TMap<int, FGMASQueueOperationHandle>&  GetEffectHandlesForTest()       { return EffectHandles; }
	bool GetEffectFromHandleForTest(int Handle, int32& OutNetId, UGMCAbilityEffect*& OutEffect) const
	{
		return GetEffectFromHandle(Handle, OutNetId, OutEffect);
	}

	// Test seam for the replay-skip gate in TickActiveEffects. CL_IsReplaying() on
	// the GMC movement component is non-virtual so the headless harness can't
	// override its behaviour through inheritance. Setting this flag forces
	// IsReplayingForGMASLogic() to return true, exercising the production code
	// path that skips polling/timeout reaping during replay.
	bool bForceReplayingForTest = false;

	// Test seam for ServerProcessOperation. The function is private so tests cannot
	// call it directly; this thin wrapper exposes it under WITH_AUTOMATION_WORKER only.
	void ServerProcessOperationForTest(const FInstancedStruct& OperationData, bool bFromMovementTick)
	{
		ServerProcessOperation(OperationData, bFromMovementTick);
	}

	// Test seam for the HasAuthority() guard in ServerProcessOperation. Orphan components
	// in the headless harness always report HasAuthority()==false; setting this flag forces
	// IsAuthorityForGMASLogic() to return true so server-side dispatch paths can be exercised.
	bool bForceAuthorityForTest = false;

	// Test seam: pre-seed BoundQueueV2.OperationData with a valid base struct so that
	// IsValidGMASOperation() passes during headless-harness ServerProcessOperation calls.
	// Must be called before ServerProcessOperationForTest when BindReplicationData() has
	// not been invoked (i.e. in specs where the GMC move loop is not running).
	void SeedBoundQueueOperationDataForTest(int OperationID)
	{
		BoundQueueV2.OperationData = FInstancedStruct::Make<FGMASBoundQueueV2OperationBaseData>(
			FGMASBoundQueueV2OperationBaseData{OperationID});
	}

	// Test-only mutable accessor to BoundQueueV2 internals. Lets specs seed
	// ClientQueuedOperations / OperationPayloads and call drain helpers directly
	// without exposing the queue to production callers. Returning a reference
	// keeps the call sites symmetric with how internal functions touch it.
	FGMASBoundQueueV2& GetBoundQueueV2ForTest() { return BoundQueueV2; }

	// Test seam for TryActivateClientAuthAbility. The function is private so tests cannot
	// call it directly; this thin wrapper exposes it under WITH_AUTOMATION_WORKER only.
	// Allows headless specs to validate the helper without going through QueueAbility's
	// role gate (ROLE_AutonomousProxy / ROLE_Authority), which fails for orphan components.
	bool TryActivateClientAuthAbilityForTest(TSubclassOf<UGMCAbility> AbilityClass,
	                                         FGameplayTag InputTag,
	                                         const UInputAction* InputAction)
	{
		return TryActivateClientAuthAbility(AbilityClass, InputTag, InputAction);
	}

	// Test seam for CheckActivationTagsForClientAuth. The function is protected so tests
	// cannot call it directly; this thin wrapper exposes it under WITH_AUTOMATION_WORKER only.
	bool CheckActivationTagsForClientAuthForTest(const UGMCAbility* Ability) const
	{
		return CheckActivationTagsForClientAuth(Ability);
	}
private:
#endif

public:
	// Centralized "are we currently inside a GMC replay?" check used by GMAS-side
	// logic that must skip mutating non-bound state during replay re-execution
	// (polling promotion, Pending+timeout reap). Production wraps the GMC's own
	// CL_IsReplaying(); test builds layer a per-component override on top.
	bool IsReplayingForGMASLogic() const;

	// Centralized authority check for GMAS server-side dispatch. Production delegates to
	// AActor::HasAuthority(); test builds layer bForceAuthorityForTest on top so that
	// ServerProcessOperation can be exercised in the headless harness where orphan
	// components always report HasAuthority()==false.
	bool IsAuthorityForGMASLogic() const;

	// Networked FX
	// Is this ASC locally controlled?
	bool IsLocallyControlledPawnASC() const;
	
	// Spawn a Niagara system attached to a component
	// IsClientPredicted: If true, the system will be spawned on the client immediately. False, the local client will spawn it when the multicast is received
	// bDelayByGMCSmoothing: If true, the system will be spawned with a delay for SimProxies to match the smoothing delay
	UFUNCTION(BlueprintCallable, Category="GMAS|FX")
	UNiagaraComponent* SpawnParticleSystemAttached(FFXSystemSpawnParameters SpawnParams, bool bIsClientPredicted = false, bool bDelayByGMCSmoothing = false);

	UFUNCTION(NetMulticast, Unreliable)
	void MC_SpawnParticleSystemAttached(const FFXSystemSpawnParameters& SpawnParams, bool bIsClientPredicted = false, bool bDelayByGMCSmoothing = false);

		
	// Spawn a Niagara system at a world location
	// IsClientPredicted: If true, the system will be spawned on the client immediately. False, the local client will spawn it when the multicast is received
	// bDelayByGMCSmoothing: If true, the system will be spawned with a delay for SimProxies to match the smoothing delay
	UFUNCTION(BlueprintCallable, Category="GMAS|FX")
	UNiagaraComponent* SpawnParticleSystemAtLocation(FFXSystemSpawnParameters SpawnParams, bool bIsClientPredicted = false, bool bDelayByGMCSmoothing = false);

	UFUNCTION(NetMulticast, Unreliable)
	void MC_SpawnParticleSystemAtLocation(const FFXSystemSpawnParameters& SpawnParams, bool bIsClientPredicted = false, bool bDelayByGMCSmoothing = false);

	
	// Spawn a Sound at the given location
	UFUNCTION(BlueprintCallable, Category="GMAS|FX")
	void SpawnSound(USoundBase* Sound, FVector Location, float VolumeMultiplier = 1.f, float PitchMultiplier = 1.f, bool bIsClientPredicted = false);


	UFUNCTION(NetMulticast, Unreliable)
	void MC_SpawnSound(USoundBase* Sound, FVector Location, float VolumeMultiplier = 1.f, float PitchMultiplier = 1.f, bool bIsClientPredicted = false);

	friend class FGameplayDebuggerCategory_GMCAbilitySystem;
};
