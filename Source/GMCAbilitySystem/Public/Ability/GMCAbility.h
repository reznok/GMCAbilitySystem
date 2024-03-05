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
	void Tick(float DeltaTime);
	
	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="Tick Ability"), Category="GMCAbilitySystem|Ability")
	void TickEvent(float DeltaTime);
	
	UFUNCTION()
	void BeginAbility();
	
	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="Begin Ability"), Category="GMCAbilitySystem|Ability")
	void BeginAbilityEvent();

	UFUNCTION(BlueprintCallable, meta=(DisplayName="End Ability"), Category="GMCAbilitySystem|Ability")
	void EndAbility();

	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="End Ability"), Category="GMCAbilitySystem|Ability")
	void EndAbilityEvent();

	UFUNCTION(BlueprintPure, Category="GMCAbilitySystem|Ability")
	AActor* GetOwnerActor() const;

	// Get Ability Owner Attribute value by Name from a passed AbilityComponent
	UFUNCTION(BlueprintPure, Category="GMCAbilitySystem|Ability")
	float GetOwnerAttributeValueByName(FName Attribute) const;

	// Get Ability Owner Attribute value by Name from a passed AbilityComponent
	UFUNCTION(BlueprintPure, Category="GMCAbilitySystem|Ability")
	float GetOwnerAttributeValueByTag(FGameplayTag AttributeTag) const;

	// Get Attribute value by Name from a passed AbilityComponent
	UFUNCTION(BlueprintPure, Category="GMCAbilitySystem|Ability")
	static float GetAttributeValueByName(const UGMC_AbilitySystemComponent* AbilityComponent, FName Attribute);

	// Get Attribute value by Name from a passed AbilityComponent
	UFUNCTION(BlueprintPure, Category="GMCAbilitySystem|Ability")
	static float GetAttributeValueByTag(const UGMC_AbilitySystemComponent* AbilityComponent, FGameplayTag AttributeTag);

	UFUNCTION(BlueprintCallable, Category="GMCAbilitySystem|Ability")
	void SetOwnerJustTeleported(bool bValue);
	
	UPROPERTY(EditAnywhere, Category="Ability", meta=(Categories="Ability"))
	FGameplayTag AbilityTag;
	
	UPROPERTY(EditAnywhere)
	TSubclassOf<UGMCAbilityEffect> AbilityCost;

	UFUNCTION(BlueprintCallable)
	void CommitAbilityCost();

	UPROPERTY(BlueprintReadOnly)
	UGMC_AbilitySystemComponent* OwnerAbilityComponent;

	UFUNCTION(BlueprintPure)
	UGMC_MovementUtilityCmp* GetOwnerMovementComponent() const {return OwnerAbilityComponent->GMCMovementComponent; };
	
	UPROPERTY()
	UInputAction* AbilityKey;
	
	void ProgressTask(int TaskID, FInstancedStruct TaskData);

	// UFUNCTION(BlueprintCallable)
	// bool HasAuthority();

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

	int AbilityID = -1;
	int TaskIDCounter = -1;

	/** List of currently active tasks, do not modify directly */
	UPROPERTY()
	TArray<TObjectPtr<UGameplayTask>>	ActiveTasks;

	bool CheckActivationTags();

};


