#pragma once
#include "GMCAbilityEffect.h"
#include "InputAction.h"
#include "Tasks/GMCAbilityTaskBase.h"
#include "GMCAbility.generated.h"

UENUM()
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
		GrantedAbilityIndex = -1;
		AbilityActivationID = AbilityActivationIDCounter++;
		TargetVector0 = FVector::Zero();
		TargetVector1 = FVector::Zero();
		TargetVector2 = FVector::Zero();
		TargetActor = nullptr;
		TargetComponent = nullptr;
		bProgressTask = false;
		ActionInput = nullptr;
		TaskID = -1;
	}

	FGMCAbilityData(FGMCAbilityData const& Other)
	{
		*this = Other;
	}
	
	bool operator==(FGMCAbilityData const& Other) const;
	FString ToStringSimple() const;

	// // State of Ability execution
	// UPROPERTY(BlueprintReadWrite)
	// TEnumAsByte<EAbilityState> AbilityState;

	// GUID to reference the specific ability data
	// https://forums.unrealengine.com/t/fs-test-id-is-not-initialized-properly/560690/5 for why the meta is needed
	UPROPERTY(BlueprintReadOnly, meta = (IgnoreForMemberInitializationTest))
	int AbilityActivationID;

	inline static int AbilityActivationIDCounter;
	
	// Ability ID to cast
	UPROPERTY(BlueprintReadWrite)
	int GrantedAbilityIndex;

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
class GMCABILITYSYSTEM_API UGMCAbility : public UObject
{
	GENERATED_BODY()

	UFUNCTION(BlueprintCallable)
	virtual UWorld* GetWorld() const override;
	
public:
	
	//// Ability State
	// EAbilityState. Use Getters/Setters
	UPROPERTY()
	EAbilityState AbilityState;

	UPROPERTY()
	int TaskID = -1;

	UFUNCTION()
	int GetNextTaskID(){TaskID += 1; return TaskID;};

	UPROPERTY()
	TMap<int, UGMCAbilityTaskBase*> RunningTasks;

	void RegisterTask(int Id, UGMCAbilityTaskBase* Task) {RunningTasks.Add(Id, Task);}
	
	void Execute(FGMCAbilityData AbilityData, UGMC_AbilityComponent* InAbilityComponent);

	UFUNCTION()
	void BeginAbility(FGMCAbilityData AbilityData);
	
	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="Begin Ability"), Category="GMCAbilitySystem")
	void BeginAbilityBP(FGMCAbilityData AbilityData);

	UFUNCTION(BlueprintCallable, meta=(DisplayName="End Ability"), Category="GMCAbilitySystem")
	void EndAbility();
	
	UPROPERTY(EditAnywhere)
	TSubclassOf<UGMCAbilityEffect> AbilityCost;

	UFUNCTION(BlueprintCallable)
	void CommitAbilityCost();

	UPROPERTY(BlueprintReadOnly)
	UGMC_AbilityComponent* AbilityComponent;

	UPROPERTY(BlueprintReadOnly)
	FGMCAbilityData InitialAbilityData;
	
	void CompleteLatentTask(int TaskID);

	UFUNCTION(BlueprintCallable)
	bool HasAuthority();

	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool AbilityEnded() {return AbilityState == EAbilityState::Ended;};
};


