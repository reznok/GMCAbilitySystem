#pragma once
#include "GMCAbilityTaskBase.generated.h"

class UGMCAbility;
class UGMC_AbilityComponent;
class UGameplayTasksComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGMCAbilityTaskOutputPin);

UCLASS(Abstract, BlueprintType, meta = (ExposedAsyncProxy=AsyncTask), config = Game)
class GMCABILITYSYSTEM_API UGMCAbilityTaskBase : public UGameplayTask
{
	GENERATED_BODY()
	
public:
	
	// UPROPERTY()
	// UGMCAbility* OwningAbility;

	UPROPERTY()
	int TaskID;

	FDelegateHandle TickHandle;

	void SetAbilitySystemComponent(UGMC_AbilityComponent* InAbilitySystemComponent);

	/** GameplayAbility that created us */
	UPROPERTY()
	TWeakObjectPtr<UGMCAbility> Ability;

	UPROPERTY()
	TWeakObjectPtr<UGMC_AbilityComponent> AbilitySystemComponent;

	// /** Returns true if the ability is a locally predicted ability running on a client. Usually this means we need to tell the server something. */
	// bool IsPredictingClient() const;
	//
	// /** Returns true if we are executing the ability on the server for a non locally controlled client */
	// bool IsForRemoteClient() const;
	//
	// /** Returns true if we are executing the ability on the locally controlled client */
	// bool IsLocallyControlled() const;

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

	UPROPERTY(BlueprintAssignable)
	FGMCAbilityTaskOutputPin Completed;
	

	//void CompleteTask(bool Forced) {InternalCompleted(Forced);};

protected:
	bool bTaskCompleted;

	/** Task Owner that created us */
	TWeakObjectPtr<AActor> TaskOwner;
};
