#pragma once
#include "GameplayTagContainer.h"
#include "GMCAbilityData.h"
#include "GMCAbilitySystem.h"
#include "GameplayTaskOwnerInterface.h"
#include "GMCAbilityComponent.h"
#include "InstancedStruct.h"
#include "Effects/GMCAbilityEffect.h"
#include "GMCAbility.generated.h"

UENUM(BlueprintType)
enum class EAbilityState : uint8
{
	Initialized,
	Running,
	Waiting,
	Ended
};


// Forward Declarations
class UGMC_AbilitySystemComponent;
class UGMCAbilityTaskBase;
class UGMC_MovementUtilityCmp;

UCLASS(Blueprintable, BlueprintType)
class GMCABILITYSYSTEM_API UGMCAbility : public UObject, public IGameplayTaskOwnerInterface
{
	GENERATED_BODY()

public:
	
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	virtual UWorld* GetWorld() const override;
	
	//// Ability State
	// EAbilityState. Use Getters/Setters
	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	EAbilityState AbilityState;

	// Data used to execute this ability
	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	FGMCAbilityData AbilityData;

	// Assign a new, incrementing, Task ID
	UFUNCTION()
	int GetNextTaskID(){TaskIDCounter += 1;
		return TaskIDCounter;}
	

	int GetAbilityID() const {return AbilityID;};;
	
	UPROPERTY()
	TMap<int, UGMCAbilityTaskBase*> RunningTasks;

	void RegisterTask(int Id, UGMCAbilityTaskBase* Task) {RunningTasks.Add(Id, Task);}
	void TickTasks(float DeltaTime);
	void AncillaryTickTasks(float DeltaTime);
	
	void Execute(UGMC_AbilitySystemComponent* InAbilityComponent, int InAbilityID, const UInputAction* InputAction = nullptr);
	
	// Called by AbilityComponent (this is a prediction tick so should be used for movement)
	virtual void Tick(float DeltaTime);
	
	// Called by AbilityComponent from AncillaryTick (won't be rolled back on mispredictions)
	virtual void AncillaryTick(float DeltaTime);
	
	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="Tick Ability"), Category="GMCAbilitySystem|Ability")
	void TickEvent(float DeltaTime);

	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="Ancillary Tick Ability"), Category="GMCAbilitySystem|Ability")
	void AncillaryTickEvent(float DeltaTime);

	UFUNCTION(BlueprintNativeEvent, meta=(DisplayName="Ability PreExecution Check"), Category="GMCAbilitySystem|Ability")
	bool PreExecuteCheckEvent();

	UFUNCTION()
	virtual bool PreBeginAbility();
	
	UFUNCTION()
	virtual void BeginAbility();
	
	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="Begin Ability", Keywords = "BeginPlay"), Category="GMCAbilitySystem|Ability")
	void BeginAbilityEvent();

	UFUNCTION(BlueprintCallable, meta=(DisplayName="End Ability"), Category="GMCAbilitySystem|Ability")
	virtual void EndAbility();

	/** End an ability without triggering the EndAbilityEvent.
	 * This is useful for abilities that need to end immediately without any additional logic, usual for dead born abilities. */
	UFUNCTION(BlueprintCallable, meta=(DisplayName="Cancel Ability"), Category="GMCAbilitySystem|Ability")
	virtual void CancelAbility();

	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="End Ability"), Category="GMCAbilitySystem|Ability")
	void EndAbilityEvent();

	UFUNCTION(BlueprintPure, Category="GMCAbilitySystem|Ability")
	AActor* GetOwnerActor() const;

	/** Get the Pawn associated with ability if applicable. */
	UFUNCTION(BlueprintPure, Category="GMCAbilitySystem|Ability")
	AGMC_Pawn* GetOwnerPawn() const;

	/** Get the Player Controller associated with the owning pawn if applicable. */
	UFUNCTION(BlueprintPure, Category="GMCAbilitySystem|Ability")
	AGMC_PlayerController* GetOwningPlayerController() const;

	// Get Ability Owner Attribute value by Name from a passed AbilityComponent
	UFUNCTION(BlueprintPure, Category="GMCAbilitySystem|Ability")
	float GetOwnerAttributeValueByTag(FGameplayTag AttributeTag) const;

	UFUNCTION(BlueprintCallable, Category="GMCAbilitySystem|Ability")
	void SetOwnerJustTeleported(bool bValue);

	// Tag to identify this ability. Required for setting cooldowns.
	UPROPERTY(EditAnywhere, meta=(Categories="Ability"), Category = "GMCAbilitySystem")
	FGameplayTag AbilityTag;

	// An Effect that modifies attributes when the ability is activated
	UPROPERTY(EditAnywhere, Category = "GMCAbilitySystem")
	TSubclassOf<UGMCAbilityEffect> AbilityCost;

	// How long in seconds ability should go on cooldown when activated
	// Requires AbilityTag to be set
	UPROPERTY(EditAnywhere, Category = "GMCAbilitySystem")
	float CooldownTime;

	// If true, the ability will apply the Cooldown when activated
	// If false, the ability will NOT apply the Cooldown when the ability begins
	// You can still apply the cooldown manually with CommitAbilityCooldown or CommitAbilityCostAndCooldown
	UPROPERTY(EditAnywhere, Category = "GMCAbilitySystem")
	bool bApplyCooldownAtAbilityBegin{true};

	// If true, more than one instance of this ability can be active at once. If false, the actual activation (but not
	// the queuing) of an ability will fail if the ability already is active.
	UPROPERTY(EditAnywhere, Category = "GMCAbilitySystem")
	bool bAllowMultipleInstances {false};
	
	// Check to see if affected attributes in the AbilityCost would still be >= 0 after committing the cost
	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	virtual bool CanAffordAbilityCost() const;

	// Apply the effects in AbilityCost and (Re-)apply the CooldownTime of this ability
	// Warning : Will apply CooldownTime regardless of already being on cooldown
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	virtual void CommitAbilityCostAndCooldown();
	
	// (Re-)Apply the CooldownTime of this ability
	// Warning : Will apply CooldownTime regardless of already being on cooldown
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	virtual void CommitAbilityCooldown();
	
	// Apply the effects in AbilityCost
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	virtual void CommitAbilityCost();

	// Remove the ability cost effect (if applicable)
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	virtual void RemoveAbilityCost();

	// Live modifying the BlockOtherAbility tags
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	virtual void ModifyBlockOtherAbility(FGameplayTagContainer TagToAdd, FGameplayTagContainer TagToRemove);

	// Reset the BlockOtherAbility tags to the default values
	UFUNCTION(BlueprintCallable, Category = "GMCAbilitySystem")
	virtual void ResetBlockOtherAbility();

	// GMC_AbilitySystemComponent that owns this ability
	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	UGMC_AbilitySystemComponent* OwnerAbilityComponent;

	// The GMC Movement Component on the same actor as OwnerAbilityComponent
	UFUNCTION(BlueprintPure, Category = "GMCAbilitySystem")
	UGMC_MovementUtilityCmp* GetOwnerMovementComponent() const {return OwnerAbilityComponent->GMCMovementComponent; };
	
	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	TObjectPtr<const UInputAction> AbilityInputAction;

	// Pass data into the Task
	void HandleTaskData(int TaskID, FInstancedStruct TaskData);

	void HandleTaskHeartbeat(int TaskID);

	// UFUNCTION(BlueprintCallable)
	// bool HasAuthority();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "GMCAbilitySystem")
	bool AbilityEnded() {return AbilityState == EAbilityState::Ended;};

	// Tags
	// Tags that the owner must have to activate the ability. BeginAbility will not be called if the owner does not have these tags.
	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem")
	FGameplayTagContainer ActivationRequiredTags;

	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem")
	// Tags that the owner must not have to activate ability. BeginAbility will not be called if the owner has these tags.
	FGameplayTagContainer ActivationBlockedTags;

	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem", meta=(Categories="Ability"))
	// Cancel Abilities with these tags when this ability is activated
	FGameplayTagContainer CancelAbilitiesWithTag;

	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem", meta=(Categories="Ability"))
	// Prevent Abilities with these tags from activating when this ability is activated
	FGameplayTagContainer BlockOtherAbility;

	/** 
	 * If true, activate on movement tick, if false, activate on ancillary tick. Defaults to true.
	 * Should be set to false for actions that should not be replayed on mispredictions. i.e. firing a weapon
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GMCAbilitySystem")
	bool bActivateOnMovementTick = true; 

	UFUNCTION()
	void ServerConfirm();

	UFUNCTION()
	void SetPendingEnd();
	
	// --------------------------------------
	//	IGameplayTaskOwnerInterface
	// --------------------------------------	
	virtual UGameplayTasksComponent* GetGameplayTasksComponent(const UGameplayTask& Task) const override;
	virtual AActor* GetGameplayTaskOwner(const UGameplayTask* Task) const override;
	virtual AActor* GetGameplayTaskAvatar(const UGameplayTask* Task) const override;
	virtual void OnGameplayTaskInitialized(UGameplayTask& Task) override;
	virtual void OnGameplayTaskActivated(UGameplayTask& Task) override;
	virtual void OnGameplayTaskDeactivated(UGameplayTask& Task) override;
	
private:

	void FinishEndAbility();

	int AbilityID = -1;
	int TaskIDCounter = -1;

	bool bServerConfirmed = false;

	bool bEndPending = false;

	float ClientStartTime;
	
	// How long to wait for server to confirm ability before cancelling on client
	float ServerConfirmTimeout = 1.f;

	/** List of currently active tasks, do not modify directly */
	UPROPERTY()
	TArray<TObjectPtr<UGameplayTask>> ActiveTasks;

	UPROPERTY()
	UGMCAbilityEffect* AbilityCostInstance = nullptr;

	bool IsOnCooldown() const;

public:
	FString ToString() const{
		return FString::Printf(TEXT("[name: ] %s (State %s) [Tag %s] | NumTasks %d"), *GetName(), *EnumToString(AbilityState), *AbilityTag.ToString(), RunningTasks.Num());
	}

};

