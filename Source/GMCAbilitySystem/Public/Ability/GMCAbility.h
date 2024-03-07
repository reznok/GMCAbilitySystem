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

	UFUNCTION(BlueprintCallable)
	virtual UWorld* GetWorld() const override;
	
public:
		//// Ability State
	// EAbilityState. Use Getters/Setters
	UPROPERTY(BlueprintReadOnly)
	EAbilityState AbilityState;

	// Assign a new, incrementing, Task ID
	UFUNCTION()
	int GetNextTaskID(){TaskIDCounter += 1;
		return TaskIDCounter;}
	

	int GetAbilityID() const {return AbilityID;};;
	
	UPROPERTY()
	TMap<int, UGMCAbilityTaskBase*> RunningTasks;

	void RegisterTask(int Id, UGMCAbilityTaskBase* Task) {RunningTasks.Add(Id, Task);}
	void TickTasks(float DeltaTime);
	
	void Execute(UGMC_AbilitySystemComponent* InAbilityComponent, int InAbilityID, UInputAction* InputAction = nullptr);
	
	// Called by AbilityComponent
	virtual void Tick(float DeltaTime);
	
	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="Tick Ability"), Category="GMCAbilitySystem|Ability")
	void TickEvent(float DeltaTime);
	
	UFUNCTION()
	virtual void BeginAbility();
	
	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="Begin Ability"), Category="GMCAbilitySystem|Ability")
	void BeginAbilityEvent();

	UFUNCTION(BlueprintCallable, meta=(DisplayName="End Ability"), Category="GMCAbilitySystem|Ability")
	virtual void EndAbility();

	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="End Ability"), Category="GMCAbilitySystem|Ability")
	void EndAbilityEvent();

	UFUNCTION(BlueprintPure, Category="GMCAbilitySystem|Ability")
	AActor* GetOwnerActor() const;

	// Get Ability Owner Attribute value by Name from a passed AbilityComponent
	UFUNCTION(BlueprintPure, Category="GMCAbilitySystem|Ability")
	float GetOwnerAttributeValueByTag(FGameplayTag AttributeTag) const;

	UFUNCTION(BlueprintCallable, Category="GMCAbilitySystem|Ability")
	void SetOwnerJustTeleported(bool bValue);

	// Tag to identify this ability. Required for setting cooldowns.
	UPROPERTY(EditAnywhere, meta=(Categories="Ability"))
	FGameplayTag AbilityTag;

	// An Effect that modifies attributes when the ability is activated
	UPROPERTY(EditAnywhere)
	TSubclassOf<UGMCAbilityEffect> AbilityCost;

	// How long in seconds ability should go on cooldown when activated
	// Requires AbilityTag to be set
	UPROPERTY(EditAnywhere)
	float CooldownTime;

	// Check to see if affected attributes in the AbilityCost would still be >= 0 after committing the cost
	UFUNCTION(BlueprintPure)
	virtual bool CanAffordAbilityCost() const;

	// Apply the effects in AbilityCost
	UFUNCTION(BlueprintCallable)
	virtual void CommitAbilityCost();

	// Remove the ability cost effect (if applicable)
	UFUNCTION(BlueprintCallable)
	virtual void RemoveAbilityCost();

	// GMC_AbilitySystemComponent that owns this ability
	UPROPERTY(BlueprintReadOnly)
	UGMC_AbilitySystemComponent* OwnerAbilityComponent;

	// The GMC Movement Component on the same actor as OwnerAbilityComponent
	UFUNCTION(BlueprintPure)
	UGMC_MovementUtilityCmp* GetOwnerMovementComponent() const {return OwnerAbilityComponent->GMCMovementComponent; };
	
	UPROPERTY(BlueprintReadOnly)
	UInputAction* AbilityInputAction;

	// Pass data into the Task
	void HandleTaskData(int TaskID, FInstancedStruct TaskData);

	void HandleTaskHeartbeat(int TaskID);

	// UFUNCTION(BlueprintCallable)
	// bool HasAuthority();

	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool AbilityEnded() {return AbilityState == EAbilityState::Ended;};

	// Tags
	// Tags that the owner must have to activate the ability. BeginAbility will not be called if the owner does not have these tags.
	UPROPERTY(EditDefaultsOnly)
	FGameplayTagContainer ActivationRequiredTags;

	UPROPERTY(EditDefaultsOnly)
	// Tags that the owner must not have to activate ability. BeginAbility will not be called if the owner has these tags.
	FGameplayTagContainer ActivationBlockedTags;

	UFUNCTION()
	void ServerConfirm();
	
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

	int AbilityID = -1;
	int TaskIDCounter = -1;

	bool bServerConfirmed = false;

	float ClientStartTime;
	
	// How long to wait for server to confirm ability before cancelling on client
	float ServerConfirmTimeout = 1.f;

	/** List of currently active tasks, do not modify directly */
	UPROPERTY()
	TArray<TObjectPtr<UGameplayTask>> ActiveTasks;

	bool CheckActivationTags();

	UPROPERTY()
	UGMCAbilityEffect* AbilityCostInstance = nullptr;

	bool IsOnCooldown() const;

public:
	FString ToString() const{
		return FString::Printf(TEXT("[name: ] %s (State %s) [Tag %s] | NumTasks %d"), *GetName(), *EnumToString(AbilityState), *AbilityTag.ToString(), RunningTasks.Num());
	}

};

