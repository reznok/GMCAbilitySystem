﻿// Fill out your copyright notice in the Description page of Project Settings.

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
#include "Utility/GMASBoundQueue.h"
#include "GMCAbilityComponent.generated.h"


class UGMCAbilityAnimInstance;
class UGMCAbilityMapData;
class UGMCAttributesData;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPreAttributeChanged, UGMCAttributeModifierContainer*, AttributeModifierContainer, UGMC_AbilitySystemComponent*,
                                             SourceAbilityComponent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnAttributeChanged, FGameplayTag, AttributeTag, float, OldValue, float, NewValue);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FGameplayAttributeChangedNative, const FGameplayTag&, const float, const float);
				
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAncillaryTick, float, DeltaTime);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnActiveTagsChanged, FGameplayTagContainer, AddedTags, FGameplayTagContainer, RemovedTags);
DECLARE_MULTICAST_DELEGATE_TwoParams(FGameplayTagFilteredMulticastDelegate, const FGameplayTagContainer&, const FGameplayTagContainer&);

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

class UGMCAbility;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="GMC Ability System Component"), meta=(Categories="GMAS"))
class GMCABILITYSYSTEM_API UGMC_AbilitySystemComponent : public UGameplayTasksComponent //  : public UGMC_MovementUtilityCmp
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UGMC_AbilitySystemComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// Will apply the starting effects and abilities to the component,
	// bForce will re-apply the effects, usefull if we want to re-apply the effects after a reset (like a death)
	// Must be called on the server only
	virtual void ApplyStartingEffects(bool bForce = false);

	// Bound/Synced over GMC
	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	double ActionTimer;

	// Is this a server-only pawn (not player-controlled)?
	bool IsServerOnly() const;
	
	// Ability tags that the controller has 
	FGameplayTagContainer GetGrantedAbilities() const { return GrantedAbilityTags; }

	// Gameplay tags that the controller has
	FGameplayTagContainer GetActiveTags() const { return ActiveTags; }

	// Return the active ability effects
	TMap<int, UGMCAbilityEffect*> GetActiveEffects() const { return ActiveEffects; }

	// Return active Effect with tag
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Abilities")
	TArray<UGMCAbilityEffect*> GetActiveEffectsByTag(FGameplayTag GameplayTag) const;

	// Get the first active effect with the Effecttag
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GMAS|Abilities")
	UGMCAbilityEffect* GetFirstActiveEffectByTag(FGameplayTag GameplayTag) const;

	UFUNCTION(BlueprintCallable, Category="GMAS|Abilities")
	void AddAbilityMapData(UGMCAbilityMapData* AbilityMapData);

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

	// Add an ability to the GrantedAbilities array
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	void AddActiveTag(const FGameplayTag AbilityTag);

	// Remove an ability from the GrantedAbilities array
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	void RemoveActiveTag(const FGameplayTag AbilityTag);

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

	// Do not call directly on client, go through QueueAbility
	void TryActivateAbilitiesByInputTag(const FGameplayTag& InputTag, const UInputAction* InputAction = nullptr, bool bFromMovementTick=true);
	
	// Do not call directly on client, go through QueueAbility. Can be used to call server-side abilities (like AI).
	bool TryActivateAbility(TSubclassOf<UGMCAbility> ActivatedAbility, const UInputAction* InputAction = nullptr);
	
	// Queue an ability to be executed
	UFUNCTION(BlueprintCallable, DisplayName="Activate Ability", Category="GMAS|Abilities")
	void QueueAbility(UPARAM(meta=(Categories="Input"))FGameplayTag InputTag, const UInputAction* InputAction = nullptr);

	UFUNCTION(BlueprintCallable, DisplayName="Count Queued Ability Instances", Category="GMAS|Abilities")
	int32 GetQueuedAbilityCount(FGameplayTag AbilityTag);

	UFUNCTION(BlueprintCallable, DisplayName="Count Activated Ability Instances", Category="GMAS|Abilities")
	int32 GetActiveAbilityCount(TSubclassOf<UGMCAbility> AbilityClass);

	// Perform a check in every active ability against BlockOtherAbility and check if the tag provided is present
	bool IsAbilityTagBlocked(const FGameplayTag AbilityTag) const;

	UFUNCTION(BlueprintCallable, DisplayName="End Abilities (By Tag)", Category="GMAS|Abilities")
	// End all abilities with the corresponding tag, returns the number of abilities ended
	int EndAbilitiesByTag(FGameplayTag AbilityTag);

	UFUNCTION(BlueprintCallable, DisplayName="End Abilities (By Class)", Category="GMAS|Abilities")
	// End all abilities with the corresponding tag, returns the number of abilities ended
	int EndAbilitiesByClass(TSubclassOf<UGMCAbility> AbilityClass);
	
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

	int GetNextAvailableEffectID() const;
	bool CheckIfEffectIDQueued(int EffectID) const;
	int CreateEffectOperation(TGMASBoundQueueOperation<UGMCAbilityEffect, FGMCAbilityEffectData>& OutOperation, const TSubclassOf<UGMCAbilityEffect>& Effect, const FGMCAbilityEffectData& EffectData, bool bForcedEffectId = true, EGMCAbilityEffectQueueType QueueType = EGMCAbilityEffectQueueType::Predicted);
	
	/**
	 * Applies an effect to the Ability Component. If bOuterActivation is false, the effect will be immediately
	 * applied; if either is true, the operation will be queued but no valid effect will be returned. If
	 * Outer Activation is true, the effect *must* be applied on the server.
	 *
	 * @param	Effect		        Effect to apply
	 * @param   InitializationData  Effect initialization data.
	 * @param   bOuterActivation    Whether this effect should be replicated outside of GMC, via normal Unreal RPC
	 */
	UFUNCTION(BlueprintCallable, Category="GMAS|Effects", DisplayName="Apply Ability Effect (Legacy)", meta=(DeprecatedFunction, DeprecationMessage="Please use the more modern ApplyAbilityEffect which takes a queue type."))
	UGMCAbilityEffect* ApplyAbilityEffect(TSubclassOf<UGMCAbilityEffect> Effect, FGMCAbilityEffectData InitializationData, bool bOuterActivation = false);

	// BP-specific version of 
	
	/**
	 * Applies an effect to the ability component. If the Queue Type is Predicted, the effect will be immediately added
	 * on both client and server; this must happen within the GMC movement lifecycle for it to be valid. If the
	 * Queue Type is anything else, the effect must be queued on the server and will be replicated to the client.
	 */
	UFUNCTION(BlueprintCallable, Category="GMAS|Effects", DisplayName="Apply Ability Effect")
	void ApplyAbilityEffectSafe(TSubclassOf<UGMCAbilityEffect> EffectClass, FGMCAbilityEffectData InitializationData, EGMCAbilityEffectQueueType QueueType,
		UPARAM(DisplayName="Success") bool& OutSuccess, UPARAM(DisplayName="Effect Handle") int& OutEffectHandle, UPARAM(DisplayName="Effect Network ID") int& OutEffectId, UPARAM(DisplayName="Effect Instance") UGMCAbilityEffect*& OutEffect);

	/**
	 * Applies an effect to the ability component. If the Queue Type is Predicted, the effect will be immediately added
	 * on both client and server; this must happen within the GMC movement lifecycle for it to be valid. If the
	 * Queue Type is anything else, the effect must be queued on the server and will be replicated to the client.
	 * 
	 * @param EffectClass The class of ability effect to add.
	 * @param InitializationData The initialization data for the ability effect.
	 * @param QueueType How to queue the effect.
	 * @param OutEffectHandle A local handle to this effect, only valid locally.
	 * @param OutEffectId The newly-created effect's network ID, if one is available. Valid across server/client.
	 * @param OutEffect The newly-created effect instance, if available.
	 * @return true if the effect was applied, false otherwise.
	 */
	bool ApplyAbilityEffect(TSubclassOf<UGMCAbilityEffect> EffectClass, FGMCAbilityEffectData InitializationData, EGMCAbilityEffectQueueType QueueType, int& OutEffectHandle, int& OutEffectId, UGMCAbilityEffect*& OutEffect);

	// Do not call this directly unless you know what you are doing. Otherwise, always go through the above ApplyAbilityEffect variant!
	UGMCAbilityEffect* ApplyAbilityEffect(UGMCAbilityEffect* Effect, FGMCAbilityEffectData InitializationData);
	
	UFUNCTION(BlueprintCallable, Category="GMAS|Effects")
	UGMCAbilityEffect* GetEffectById(const int EffectId) const;

	TArray<int> EffectsMatchingTag(const FGameplayTag& Tag, int32 NumToRemove = -1) const;

	// Do not call this directly unless you know what you are doing; go through the RemoveActiveAbilityEffectSafe if
	// doing this from outside of the component, to allow queuing and sanity-check.
	UFUNCTION(BlueprintCallable, Category="GMAS|Effects")
	void RemoveActiveAbilityEffect(UGMCAbilityEffect* Effect);

	UFUNCTION(BlueprintCallable, Category="GMAS|Effects", DisplayName="Remove Active Ability Effect (Safe)")
	void RemoveActiveAbilityEffectSafe(UGMCAbilityEffect* Effect, EGMCAbilityEffectQueueType QueueType = EGMCAbilityEffectQueueType::Predicted);

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

	UFUNCTION(BlueprintCallable, Category="GMAS|Effects", DisplayName="Remove Effect by Handle")
	bool RemoveEffectByHandle(int EffectHandle, EGMCAbilityEffectQueueType QueueType);
	
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
	////

	// Called when the set of active tags changes.
	UPROPERTY(BlueprintAssignable)
	FOnActiveTagsChanged OnActiveTagsChanged;

	FGameplayTagContainer PreviousActiveTags;

	/** Returns an array of pointers to all attributes */
	TArray<const FAttribute*> GetAllAttributes() const;

	/** Get an Attribute using its Tag */
	const FAttribute* GetAttributeByTag(FGameplayTag AttributeTag) const;

	// Get Attribute value by Tag
	UFUNCTION(BlueprintPure, Category="GMAS|Attributes")
	float GetAttributeValueByTag(UPARAM(meta=(Categories="Attribute"))FGameplayTag AttributeTag) const;

	// Get Attribute value by Tag
	UFUNCTION(BlueprintPure, Category="GMAS|Attributes")
	FAttributeClamp GetAttributeClampByTag(UPARAM(meta=(Categories="Attribute"))FGameplayTag AttributeTag) const;
	
	// Set Attribute value by Tag
	// Will NOT trigger an "OnAttributeChanged" Event
	// bResetModifiers: Will reset all modifiers on the attribute to the base value. DO NOT USE if you have any active effects that modify this attribute.
	UFUNCTION(BlueprintCallable, Category="GMAS|Attributes")
	bool SetAttributeValueByTag(UPARAM(meta=(Categories="Attribute"))FGameplayTag AttributeTag, float NewValue, bool bResetModifiers = false);
	
	/** Get the default value of an attribute from the data assets. */
	UFUNCTION(BlueprintCallable, Category="GMAS|Attributes")
	float GetBaseAttributeValueByTag(UPARAM(meta=(Categories="Attribute"))FGameplayTag AttributeTag) const;
	
	// Apply modifiers that affect attributes
	UFUNCTION(BlueprintCallable, Category="GMAS|Attributes")
	void ApplyAbilityEffectModifier(FGMCAttributeModifier AttributeModifier,bool bModifyBaseValue, bool bNegateValue = false, UGMC_AbilitySystemComponent* SourceAbilityComponent = nullptr);

	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	bool bJustTeleported;

	UFUNCTION(BlueprintCallable, Category="GMAS")
	bool HasAuthority() const { return GetOwnerRole() == ROLE_Authority; }
	
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

	UFUNCTION(BlueprintCallable, Category="GMAS")
	virtual void PreRemoteMoveExecution();
	
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
	virtual void BeginPlay() override;

	// Abilities that are granted to the player (bound)
	FGameplayTagContainer GrantedAbilityTags;

	// return true if the ability is allowed to be activated considering active tags
	virtual bool CheckActivationTags(const UGMCAbility* Ability) const;

	// Effect tags that are granted to the player (bound)
	FGameplayTagContainer ActiveTags;

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
	
	// Array of data objects to initialize the component's ability map
	UPROPERTY(EditDefaultsOnly, Category="Ability")
	TArray<TObjectPtr<UGMCAbilityMapData>> AbilityMaps;
	
	// Map of Ability Tags to Ability Classes
	TMap<FGameplayTag, FAbilityMapData> AbilityMap;

	// List of filtered tag delegates to call when tags change.
	TArray<TPair<FGameplayTagContainer, FGameplayTagFilteredMulticastDelegate>> FilteredTagDelegates;

	FGameplayAttributeChangedNative NativeAttributeChangeDelegate;
	
	// Get the map from the data asset and apply that to the component's map
	void InitializeAbilityMap();
	void AddAbilityMapData(const FAbilityMapData& AbilityMapData);
	void RemoveAbilityMapData(const FAbilityMapData& AbilityMapData);

	// Add the starting ability tags to GrantedAbilities at start
	void InitializeStartingAbilities();
	
	TArray<FInstancedStruct> QueuedTaskData;

	// Queued ability operations (activate, cancel, etc.)
	TGMASBoundQueue<UGMCAbility, FGMCAbilityData> QueuedAbilityOperations;
	bool ProcessAbilityOperation(const TGMASBoundQueueOperation<UGMCAbility, FGMCAbilityData>& Operation, bool bFromMovementTick);

	TGMASBoundQueue<UGMCAbilityEffect, FGMCAbilityEffectData, false> QueuedEffectOperations;
	TGMASBoundQueue<UGMCAbilityEffect, FGMCAbilityEffectData> QueuedEffectOperations_ClientAuth;
	UGMCAbilityEffect* ProcessEffectOperation(const TGMASBoundQueueOperation<UGMCAbilityEffect, FGMCAbilityEffectData>& Operation);

	bool ShouldProcessEffectOperation(const TGMASBoundQueueOperation<UGMCAbilityEffect, FGMCAbilityEffectData>& Operation, bool bIsServer = true) const;
	void ClientQueueEffectOperation(const TGMASBoundQueueOperation<UGMCAbilityEffect, FGMCAbilityEffectData>& Operation);
	
	UFUNCTION(Client, Reliable)
	void RPCClientQueueEffectOperation(const FGMASBoundQueueRPCHeader& Header);

	// Predictions of Effect state changes
	FEffectStatePrediction EffectStatePrediction{};

	TArray<FEffectStatePrediction> QueuedEffectStates;

	

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

	// Tick Predicted and Active Effects
	void TickActiveEffects(float DeltaTime);

	// Tick active abilities, primarily the Tasks inside them
	void TickActiveAbilities(float DeltaTime);

	// Tick active abilities, but from the ancillary tick rather than prediction
	void TickAncillaryActiveAbilities(float DeltaTime);

	// Tick ability cooldowns
	void TickActiveCooldowns(float DeltaTime);

	// Active Effects with a duration affecting this component
	// Can be just normally replicated since if the client doesn't have them already
	// then prediction is already out the window

	UPROPERTY(ReplicatedUsing = OnRep_ActiveEffectsData)
	TArray<FGMCAbilityEffectData> ActiveEffectsData;

	// Max time a client will predict an effect without it being confirmed by the server before cancelling
	float ClientEffectApplicationTimeout = 1.f;

	UFUNCTION()
	void OnRep_ActiveEffectsData();

	// Check if any effects have been removed by the server and remove them locally
	void CheckRemovedEffects();

	UPROPERTY()
	TMap<int, UGMCAbilityEffect*> ActiveEffects;

	UPROPERTY()
	TMap<int, FGMASQueueOperationHandle> EffectHandles;

	int GetNextAvailableEffectHandle() const;

	UFUNCTION(BlueprintCallable, Category="GMAS|Effects")
	void GetEffectFromHandle_BP(int EffectHandle, bool& bOutSuccess, int32& OutEffectNetworkId, UGMCAbilityEffect*& OutEffect);
	
	bool GetEffectFromHandle(int EffectHandle, int32& OutEffectNetworkId, UGMCAbilityEffect*& OutEffect) const;
	bool GetEffectHandle(int EffectHandle, FGMASQueueOperationHandle& HandleData) const;

	void RemoveEffectHandle(int EffectHandle);
	
	// doesn't work ATM.
	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem", meta=(AllowPrivateAccess="true"))
	bool bInGMCTime = false;

	void ServerHandlePendingEffect(float DeltaTime);
	void ServerHandlePredictedPendingEffect(float DeltaTime);

	void ClientHandlePendingEffect();
	void ClientHandlePredictedPendingEffect();

	int LateApplicationIDCounter = 0;

	// Effect IDs that have been processed and don't need to be remade when ActiveEffectsData is replicated
	// This need to be persisted for a while
	// This never empties out so it'll infinitely grow, probably a better way to accomplish this
	UPROPERTY()
	TMap<int /*ID*/, bool /*bServerConfirmed*/> ProcessedEffectIDs;

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

	friend UGMCAbilityAnimInstance;
		
};
