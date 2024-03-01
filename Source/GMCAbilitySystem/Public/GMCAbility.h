﻿#pragma once
#include "GameplayTagContainer.h"
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

USTRUCT(BlueprintType)
struct FGMCAbilityData
{
	GENERATED_USTRUCT_BODY()

	FGMCAbilityData()
	{
		// replicated
		AbilityTag = {};
		AbilityActivationID = AbilityActivationIDCounter++;
		TargetVector0 = FVector::Zero();
		TargetVector1 = FVector::Zero();
		TargetVector2 = FVector::Zero();
		TargetActor = nullptr;
		TargetComponent = nullptr;
		bProgressTask = false;
		TaskID = -1;

		// non replicated
		ActionInput = nullptr;
	}

	FGMCAbilityData(FGMCAbilityData const& Other)
	{
		*this = Other;
	}
	
	bool operator==(FGMCAbilityData const& Other) const;
	FString ToStringSimple() const;
	
	// ID to reference the specific ability data
	// https://forums.unrealengine.com/t/fs-test-id-is-not-initialized-properly/560690/5 for why the meta is needed
	UPROPERTY(BlueprintReadOnly, meta = (IgnoreForMemberInitializationTest))
	int AbilityActivationID;

	inline static int AbilityActivationIDCounter;
	
	// Ability ID to cast
	UPROPERTY(BlueprintReadWrite)
	FGameplayTag AbilityTag;

	// Generic targeting data that can be used for anything
	UPROPERTY(BlueprintReadWrite)
	FVector TargetVector0;
	
	UPROPERTY(BlueprintReadWrite)
	FVector TargetVector1;
	
	UPROPERTY(BlueprintReadWrite)
	FVector TargetVector2;

	// Target Actor
	UPROPERTY(BlueprintReadWrite)
	AActor* TargetActor;

	// Target Component
	UPROPERTY(BlueprintReadWrite)
	UActorComponent* TargetComponent;
	
	bool bProcessed = true;

	// The input used to start the ability on the client
	// Needed for things like "WaitForKeyRelease"
	UPROPERTY()
	UInputAction* ActionInput;
	
	// Used to continue execution of blueprints with waiting latent nodes where client can progress execution
	// Ie: Waiting for a key to be released
	UPROPERTY(BlueprintReadWrite)
	bool bProgressTask;

	// Task to continue ability execution on
	UPROPERTY(BlueprintReadWrite)
	int TaskID;
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


