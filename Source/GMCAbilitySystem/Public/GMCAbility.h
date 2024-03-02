#pragma once
#include "GameplayTagContainer.h"
#include "GMCAbilityData.h"
#include "Effects/GMCAbilityEffect.h"
#include "InputAction.h"
#include "Tasks/GMCAbilityTaskBase.h"
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
class UGMC_AbilityComponent;

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
	int GetNextTaskID(){TaskIDCounter += 1; return TaskIDCounter;}

	// Called by AbilityComponent
	void Tick(float DeltaTime);;
	
	UPROPERTY()
	TMap<int, UGMCAbilityTaskBase*> RunningTasks;

	void RegisterTask(int Id, UGMCAbilityTaskBase* Task) {RunningTasks.Add(Id, Task);}
	void TickTasks(float DeltaTime);
	
	void Execute(UGMC_AbilityComponent* InAbilityComponent, FGMCAbilityData AbilityData = {});

	UFUNCTION()
	void BeginAbility(FGMCAbilityData AbilityData);
	
	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="Begin Ability"), Category="GMCAbilitySystem")
	void BeginAbilityEvent(FGMCAbilityData AbilityData);

	UFUNCTION(BlueprintCallable, meta=(DisplayName="End Ability"), Category="GMCAbilitySystem")
	void EndAbility();

	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="End Ability"), Category="GMCAbilitySystem")
	void EndAbilityEvent();

	UPROPERTY(EditAnywhere)
	FGameplayTag AbilityTag;
	
	UPROPERTY(EditAnywhere)
	TSubclassOf<UGMCAbilityEffect> AbilityCost;

	UFUNCTION(BlueprintCallable)
	void CommitAbilityCost();

	UPROPERTY(BlueprintReadOnly)
	UGMC_AbilityComponent* AbilityComponent;

	UPROPERTY(BlueprintReadOnly)
	FGMCAbilityData InitialAbilityData;
	
	void ProgressTask(int TaskID);

	UFUNCTION(BlueprintCallable)
	bool HasAuthority();

	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool AbilityEnded() {return AbilityState == EAbilityState::Ended;};

	// Tags
	// Tags that the owner must have to activate ability
	UPROPERTY(EditDefaultsOnly)
	FGameplayTagContainer ActivationRequiredTags;

	UPROPERTY(EditDefaultsOnly)
	// Tags that the owner must not have to activate ability
	FGameplayTagContainer ActivationBlockedTags;
	

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

	int TaskIDCounter = -1;

	/** List of currently active tasks, do not modify directly */
	UPROPERTY()
	TArray<TObjectPtr<UGameplayTask>>	ActiveTasks;

	bool CheckActivationTags();

};


