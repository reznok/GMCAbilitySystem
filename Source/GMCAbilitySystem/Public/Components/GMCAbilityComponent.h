// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTasksComponent.h"
#include "GMCAttributes.h"
#include "Ability/GMCAbility.h"
#include "GMCMovementUtilityComponent.h"
#include "Ability/GMCAbilityData.h"
#include "Ability/Tasks/GMCAbilityTaskData.h"
#include "Effects/GMCAbilityEffect.h"
#include "Components/ActorComponent.h"
#include "GMCAbilityComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAttributeChanged, UGMCAttributeModifierContainer*, AttributeModifierContainer, UGMC_AbilitySystemComponent*, SourceAbilityComponent);

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


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="GMC Ability System Component"))
class GMCABILITYSYSTEM_API UGMC_AbilitySystemComponent : public UGameplayTasksComponent //  : public UGMC_MovementUtilityCmp
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UGMC_AbilitySystemComponent(const FObjectInitializer& ObjectInitializer);

	// Bound/Synced over GMC
	UPROPERTY(BlueprintReadOnly)
	double ActionTimer;
	
	// Ability tags that the controller has 
	FGameplayTagContainer GetGrantedAbilities() const { return GrantedAbilityTags; }

	// Gameplay tags that the controller has
	FGameplayTagContainer GetActiveTags() const { return ActiveTags; }

	// Return the active ability effects
	TMap<int, UGMCAbilityEffect*> GetActiveEffects() const { return ActiveEffects; }

	// Add an ability to the GrantedAbilities array
	UFUNCTION(BlueprintCallable)
	void GrantAbilityByTag(FGameplayTag AbilityTag);

	// Remove an ability from the GrantedAbilities array
	UFUNCTION(BlueprintCallable)
	void RemoveGrantedAbilityByTag(FGameplayTag AbilityTag);

	// Add an ability to the GrantedAbilities array
	UFUNCTION(BlueprintCallable)
	void AddActiveTag(FGameplayTag AbilityTag);

	// Remove an ability from the GrantedAbilities array
	UFUNCTION(BlueprintCallable)
	void RemoveActiveTag(FGameplayTag AbilityTag);

	UFUNCTION(BlueprintPure)
	bool HasActiveTag(FGameplayTag GameplayTag) const;

	// Do not call directly, go through QueueAbility
	bool TryActivateAbility(FGameplayTag AbilityTag, UInputAction* InputAction = nullptr);
	
	// Queue an ability to be executed
	UFUNCTION(BlueprintCallable, DisplayName="Activate Ability", Category="Ability", meta=(Categories="Ability"))
	void QueueAbility(FGameplayTag AbilityTag, UInputAction* InputAction = nullptr);

	void QueueTaskData(const FInstancedStruct& TaskData);


	// Allows for BP instances of attributes. Attributes gets set to this.
	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<UGMCAttributeSet> StartingAttributes;
	
	UPROPERTY(BlueprintReadOnly)
	UGMCAttributeSet* Attributes;

	// Adds "AbilityCost" set in BP defaults
	UFUNCTION(BlueprintCallable)
	void ApplyAbilityCost(UGMCAbility* Ability);

	/**
	 * Applies an effect to the Ability Component
	 *
	 * @param	Effect		        Effect to apply
	 * @param	AdditionalModifiers	Additional Modifiers to apply with this effect application
	 * @param	SourceAbilityComponent	Ability Component from which this effect originated
	 * @param	bOverwriteExistingModifiers	Whether or not to replace existing modifiers that have the same name as additional modifiers. If false, will add them.
	 * @param	bAppliedByServer	Is this Effect only applied by server? Used to help client predict the unpredictable.
	 */
	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "AdditionalModifiers"))
	UGMCAbilityEffect* ApplyAbilityEffect(TSubclassOf<UGMCAbilityEffect> Effect, FGMCAbilityEffectData InitializationData);
	
	UGMCAbilityEffect* ApplyAbilityEffect(UGMCAbilityEffect* Effect, FGMCAbilityEffectData InitializationData);

	
	UFUNCTION(BlueprintCallable)
	void RemoveActiveAbilityEffect(UGMCAbilityEffect* Effect);

	UPROPERTY(BlueprintAssignable)
	FOnAttributeChanged OnAttributeChanged;
	
	// Apply modifiers that affect attributes
	UFUNCTION(BlueprintCallable)
	void ApplyAbilityEffectModifier(FGMCAttributeModifier AttributeModifier, bool bNegateValue = false, UGMC_AbilitySystemComponent* SourceAbilityComponent = nullptr);

	UPROPERTY(BlueprintReadWrite)
	bool bJustTeleported;

	// Tell client an effect was applied by server
	// Either client predicted it already and can discard the update, or it will apply
	UFUNCTION(Client, Reliable)
	void RPCServerApplyEffect(const FString& EffectClassName, FGMCAbilityEffectData EffectInitializationData);

	UFUNCTION(BlueprintCallable)
	bool HasAuthority() const { return GetOwnerRole() == ROLE_Authority; }
	
	UPROPERTY(BlueprintReadWrite)
	UGMC_MovementUtilityCmp* GMCMovementComponent;

	// GMC
	UFUNCTION(BlueprintCallable, Category="GMCAbilitySystem")
	virtual void BindReplicationData();
	
	UFUNCTION(BlueprintCallable, Category="GMCAbilitySystem")
	virtual void GenAncillaryTick(float DeltaTime, bool bIsCombinedClientMove);

	UFUNCTION(BlueprintCallable, Category="GMCAbilitySystem")
	virtual void GenPredictionTick(float DeltaTime, bool bIsReplayingPrediction = false);

	UFUNCTION(BlueprintCallable, Category="GMCAbilitySystem")
	virtual void GenSimulationTick(float DeltaTime);

	UFUNCTION(BlueprintCallable, Category="GMCAbilitySystem")
	virtual void PreLocalMoveExecution();
	
protected:
	virtual void BeginPlay() override;
	
	bool CanAffordAbilityCost(UGMCAbility* Ability);

	// Abilities that are granted to the player (bound)
	FGameplayTagContainer GrantedAbilityTags;

	// Effect tags that are granted to the player (bound)
	FGameplayTagContainer ActiveTags;

	UPROPERTY(EditAnywhere)
	TArray<TSubclassOf<UGMCAbility>> StartingAbilities;

	UPROPERTY(EditAnywhere)
	TArray<TSubclassOf<UGMCAbilityEffect>> StartingEffects;
	
	// Returns a matching granted ability by class name if that ability is in the GrantedAbilities array
	TSubclassOf<UGMCAbility> GetGrantedAbilityByTag(FGameplayTag AbilityTag);
	
	// Used to set the starting Attributes from code
	// Must be called before GMCAbilityComponent runs its BindReplicationData step
	void SetAttributes(UGMCAttributeSet* NewAttributes);


	// Sync'd containers for abilities and effects
	FGMCAbilityData AbilityData;
	FInstancedStruct TaskData = FInstancedStruct::Make(FGMCAbilityTaskData{});;

	// FInstancedStruct GMC Binding
	UFUNCTION(BlueprintCallable)
	int32 BindInstancedStruct(
	  UPARAM(Ref) FInstancedStruct& VariableToBind,
	  EGMC_PredictionMode PredictionMode,
	  EGMC_CombineMode CombineMode,
	  EGMC_SimulationMode SimulationMode,
	  EGMC_InterpolationFunction Interpolation
	)
	{
		int32 BindingIndex = -1;
				GMCMovementComponent->AliasData.InstancedStruct.BindMember(
				  VariableToBind,
				  TranslateToSyncSettings(PredictionMode, CombineMode, SimulationMode, Interpolation),
				  BindingIndex
				);
		return BindingIndex;
	}

private:

	// Cache of all Effect Asset Data
	// Todo: Convert to using a tag system like abilities
	UPROPERTY()
	TArray<UBlueprintGeneratedClass *> EffectBPClasses;
	void InitializeEffectAssetClasses();

	// Map of Ability Tags to Ability Classes
	UPROPERTY(EditDefaultsOnly, Category="Ability")
	TMap<FGameplayTag, TSubclassOf<UGMCAbility>> AbilityMap;
	void InitializeAbilityMap();

	// Add the starting ability tags to GrantedAbilities at start
	void InitializeStartingAbilities();
	
	TArray<FGMCAbilityData> QueuedAbilities;
	TArray<FInstancedStruct> QueuedTaskData;

	// Current Ability Data being processed
	// Members of this struct are bound over GMC
	// FGMCAbilityData AbilityData;

	// Predictions of Effect state changes
	FEffectStatePrediction EffectStatePrediction{};

	TArray<FEffectStatePrediction> QueuedEffectStates;

	UPROPERTY()
	TMap<int, UGMCAbility*> ActiveAbilities;

	int AbilityActivationIDCounter = 0;
	
	int GenerateAbilityID() const {return ActionTimer * 100;}
	
	// Set Attributes to either a default object or a provided TSubClassOf<UGMCAttributeSet> in BP defaults
	// This must run before variable binding
	void InstantiateAttributes();

	// Clear out abilities in the Ended state from the ActivateAbilities map
	void CleanupStaleAbilities();

	// Tick Predicted and Active Effects
	void TickActiveEffects(float DeltaTime, bool bIsReplayingPrediction);

	// Tick active abilities, primarily the Tasks inside them
	void TickActiveAbilities(float DeltaTime);

	// Active Effects with a duration affecting this component
	// Can be just normally replicated since if the client doesn't have them already
	// then prediction is already out the window

	UPROPERTY(ReplicatedUsing = OnRep_ActiveEffectsData)
	TArray<FGMCAbilityEffectData> ActiveEffectsData;

	UFUNCTION()
	void OnRep_ActiveEffectsData();

	// Check if any effects have been removed by the server and remove them locally
	void CheckRemovedEffects();

	UPROPERTY()
	TMap<int, UGMCAbilityEffect*> ActiveEffects;

	// Effect IDs that have been processed and don't need to be remade when ActiveEffectsData is replicated
	// Maybe this is a bad way to do it
	UPROPERTY()
	TMap<int /*ID*/, bool /*bServerConfirmed*/> ProcessedEffectIDs;
	
};
