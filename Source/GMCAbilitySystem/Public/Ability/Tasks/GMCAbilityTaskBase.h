#pragma once
#include "InstancedStruct.h"
#include "Ability/GMCAbility.h"
#include "Ability/Tasks/GMCAbilityTaskData.h"
#include "GMCAbilityTaskBase.generated.h"

class UGMC_AbilitySystemComponent;
class UGameplayTasksComponent;

UCLASS(Abstract, BlueprintType, meta = (ExposedAsyncProxy=AsyncTask), config = Game)
class GMCABILITYSYSTEM_API UGMCAbilityTaskBase : public UGameplayTask
{
	GENERATED_BODY()
	
public:
	
	// UPROPERTY()
	// UGMCAbility* OwningAbility;

	UPROPERTY()
	int TaskID;

	virtual void Activate() override;
	
	void SetAbilitySystemComponent(UGMC_AbilitySystemComponent* InAbilitySystemComponent);
	void RegisterTask(UGMCAbilityTaskBase* Task);

	/** GameplayAbility that created us */
	UPROPERTY()
	TWeakObjectPtr<UGMCAbility> Ability;

	UPROPERTY()
	TWeakObjectPtr<UGMC_AbilitySystemComponent> AbilitySystemComponent;

	// Tick called by AbilityComponent, different from TickTask
	virtual void Tick(float DeltaTime){};

	/** Helper function for instantiating and initializing a new task */
	template <class T>
	static T* NewAbilityTask(UGMCAbility* ThisAbility, FName InstanceName = FName())
	{
		check(ThisAbility);

		T* MyObj = NewObject<T>();
		//MyObj->InitTask(*ThisAbility, ThisAbility->GetGameplayTaskDefaultPriority());
		MyObj->InitTask(*ThisAbility, 0);

		// UGMCAbilityTaskBase::DebugRecordAbilityTaskCreatedByAbility(ThisAbility);

		MyObj->InstanceName = InstanceName;
		return MyObj;
	}

	/** Called when the ability task is waiting on remote player data. IF the remote player ends the ability prematurely, and a task with this set is still running, the ability is killed. */
	// void SetWaitingOnRemotePlayerData();
	// void ClearWaitingOnRemotePlayerData();
	// virtual bool IsWaitingOnRemotePlayerdata() const override;
	
	
	//virtual void InternalCompleted(bool Forced);
	//virtual void InternalClientCompleted();

	// Called when client requests to progress task. Task must make sure this is handled properly/securely
	virtual void ProgressTask(FInstancedStruct& TaskData){};
	
	// Client calling to progress the task forward
	// Task must make sure this is handled properly
	virtual void ClientProgressTask();
	
	//void CompleteTask(bool Forced) {InternalCompleted(Forced);};

protected:
	bool bTaskCompleted;

	/** Task Owner that created us */
	TWeakObjectPtr<AActor> TaskOwner;
};
