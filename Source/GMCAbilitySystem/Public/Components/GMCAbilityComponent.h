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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAttributeChanged, UGMCAttributeModifierContainer*, AttributeModifierContainer, UGMC_AbilityComponent*, SourceAbilityComponent);

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
class GMCABILITYSYSTEM_API UGMC_AbilityComponent : public UGameplayTasksComponent //  : public UGMC_MovementUtilityCmp
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UGMC_AbilityComponent(const FObjectInitializer& ObjectInitializer);

	// Bound/Synced over GMC
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

	bool HasActiveTag(FGameplayTag GameplayTag) const;
	
	bool TryActivateAbility(FGMCAbilityData AbilityData);

	// Queue an ability to be executed
	void QueueAbility(const FGMCAbilityData& InAbilityData);
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
	void ApplyAbilityEffectModifier(FGMCAttributeModifier AttributeModifier, bool bNegateValue = false, UGMC_AbilityComponent* SourceAbilityComponent = nullptr);

	UPROPERTY(BlueprintReadWrite)
	bool bJustTeleported;

	// Tell client an effect was applied by server
	// Either client predicted it already and can discard the update, or it will apply
	UFUNCTION(Client, Reliable)
	void RPCServerApplyEffect(const FString& EffectClassName, FGMCAbilityEffectData EffectInitializationData);

	UFUNCTION(BlueprintCallable)
	bool HasAuthority() const { return GetOwnerRole() == ROLE_Authority; }

	// Client attempt to start a server-applied effect at the estimated start time
	// This is only used with server-applied effects, not properly locally predicted effects
	void ClientPredictEffectStateChange(int EffectID, EEffectState State);

	UPROPERTY(BlueprintReadOnly)
	UGMC_MovementUtilityCmp* GMCMovementComponent;

	// GMC 
	virtual void BindReplicationData();
	virtual void GenAncillaryTick(float DeltaTime, bool bIsCombinedClientMove);
	virtual void GenPredictionTick(float DeltaTime, bool bIsReplayingPrediction = false);
	virtual void GenSimulationTick(float DeltaTime);
	virtual void PreLocalMoveExecution(const FGMC_Move& LocalMove);
	
protected:
	virtual void BeginPlay() override;
	
	bool CanAffordAbilityCost(UGMCAbility* Ability);

	// Abilities that are granted to the player (bound)
	FGameplayTagContainer GrantedAbilityTags;

	// Effect tags that are granted to the player (bound)
	FGameplayTagContainer ActiveTags;

	UPROPERTY(EditAnywhere)
	TArray<TSubclassOf<UGMCAbility>> StartingAbilities;

	// Returns a matching granted ability by class name if that ability is in the GrantedAbilities array
	TSubclassOf<UGMCAbility> GetGrantedAbilityByTag(FGameplayTag AbilityTag);
	
	// Used to set the starting Attributes from code
	// Must be called before GMCAbilityComponent runs its BindReplicationData step
	void SetAttributes(UGMCAttributeSet* NewAttributes);

	UPROPERTY(EditAnywhere)
	FGMCAbilityData AbilityData;

	UPROPERTY(EditAnywhere, NoClear, Meta = (ExcludeBaseStruct, BaseStruct = "FTaskData"))
	FInstancedStruct TaskData = FInstancedStruct::Make(FGMCAbilityTaskData{});;

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
		UE_LOG(LogTemp, Warning, TEXT("Binding!"))
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
	UPROPERTY()
	TMap<FGameplayTag, UBlueprintGeneratedClass *> AbilityMap;
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
