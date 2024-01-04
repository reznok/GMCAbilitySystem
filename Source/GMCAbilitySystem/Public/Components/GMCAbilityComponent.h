// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTasksComponent.h"
#include "GMCAttributes.h"
#include "GMCAbility.h"
#include "GMCAbilityEffect.h"
#include "GMCMovementUtilityComponent.h"
#include "Components/ActorComponent.h"
#include "GMCAbilityComponent.generated.h"

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

DECLARE_MULTICAST_DELEGATE_OneParam(FGMCAbilitySystemComponentUpdateSignature, float DeltaTime);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="GMC Ability System Component"))
class GMCABILITYSYSTEM_API UGMC_AbilityComponent : public UGameplayTasksComponent //  : public UGMC_MovementUtilityCmp
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UGMC_AbilityComponent(const FObjectInitializer& ObjectInitializer);

	
	FGMCAbilitySystemComponentUpdateSignature OnFGMCAbilitySystemComponentTickDelegate;

	bool TryActivateAbility(FGMCAbilityData AbilityData);

	// Queue an ability to be executed
	void QueueAbility(const FGMCAbilityData& InAbilityData);

	// Allows for BP instances of attributes. Attributes gets set to this.
	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<UGMCAttributeSet> StartingAttributes;

	UPROPERTY(EditDefaultsOnly)
	TArray<TSubclassOf<UGMCAbilityEffect>> StartingEffects;
	
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
	 * @param	bOverwriteExistingModifiers	Whether or not to replace existing modifiers that have the same name as additional modifiers. If false, will add them.
	 * @param	bAppliedByServer	Is this Effect only applied by server? Used to help client predict the unpredictable.
	 */
	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "AdditionalModifiers"))
	UGMCAbilityEffect* ApplyAbilityEffect(TSubclassOf<UGMCAbilityEffect> Effect, TArray<FGMCAttributeModifier> AdditionalModifiers, bool bOverwriteExistingModifiers = true, bool bAppliedByServer = false);
	
	UGMCAbilityEffect* ApplyAbilityEffect(UGMCAbilityEffect* Effect, bool bServerApplied = false, FGMCAbilityEffectData InitializationData = {});

	
	UFUNCTION(BlueprintCallable)
	void RemoveActiveAbilityEffect(UGMCAbilityEffect* Effect);
	
	// Apply the modifiers that affect attributes
	void ApplyAbilityEffectModifiers(UGMCAbilityEffect* Effect);
	// Remove the modifiers that affect attributes. For the end of duration effects.
	void RemoveActiveAbilityModifiers(UGMCAbilityEffect* Effect);

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
	virtual void GenPredictionTick(float DeltaTime);
	virtual void GenSimulationTick(float DeltaTime);
	virtual void PreLocalMoveExecution(const FGMC_Move& LocalMove);
	
protected:
	virtual void BeginPlay() override;
	
	bool CanAffordAbilityCost(UGMCAbility* Ability);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Abilities")
	TArray<TSubclassOf<UGMCAbility>> GrantedAbilities;

	// Used to set the starting Attributes from code
	// Must be called before GMCAbilityComponent runs its BindReplicationData step
	void SetAttributes(UGMCAttributeSet* NewAttributes);

private:
	TArray<FGMCAbilityData> QueuedAbilities;

	// Current Ability Data being processed
	// Members of this struct are bound over GMC
	FGMCAbilityData AbilityData{};

	// Predictions of Effect state changes
	FEffectStatePrediction EffectStatePrediction{};

	TArray<FEffectStatePrediction> QueuedEffectStates;

	UPROPERTY()
	TMap<int, UGMCAbility*> ActiveAbilities;

	// Set Attributes to either a default object or a provided TSubClassOf<UGMCAttributeSet> in BP defaults
	// This must run before variable binding
	void InstantiateAttributes();

	// Clear out abilities in the Ended state from the ActivateAbilities map
	void CleanupStaleAbilities();

	// Cache all Effect Asset Data from the Asset Manager
	void InitializeEffectAssetData();

	// Tick Predicted and Active Effects
	void TickActiveEffects(float DeltaTime);

	void UpdateEffectState(FEffectStatePrediction EffectState);

	// Active Effects with a duration affecting this component0
	// These are Server Confirmed and have an ID assigned by the server
	UPROPERTY()
	TMap<int, UGMCAbilityEffect*> ActiveEffects;

	// Effects that have a duration that the client has predicted will be applied
	UPROPERTY()
	TArray<UGMCAbilityEffect*> PredictedActiveEffects;

	UGMCAbilityEffect* GetMatchingPredictedEffect(UGMCAbilityEffect* InEffect);

	// Get a new, incremented, Effect ID
	int GetNextEffectID(){EffectIDCounter += 1; return EffectIDCounter;};
	int EffectIDCounter = -1;
};
