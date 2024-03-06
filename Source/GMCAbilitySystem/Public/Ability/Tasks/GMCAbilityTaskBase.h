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
	UGMCAbility* Ability;

	UPROPERTY()
	TWeakObjectPtr<UGMC_AbilitySystemComponent> AbilitySystemComponent;

	// Tick called by AbilityComponent, different from TickTask
	virtual void Tick(float DeltaTime);

	/** Helper function for instantiating and initializing a new task */
	template <class T>
	static T* NewAbilityTask(UGMCAbility* ThisAbility, FName InstanceName = FName())
	{
		check(ThisAbility);

		T* MyObj = NewObject<T>();
		MyObj->InitTask(*ThisAbility, 0);

		// UGMCAbilityTaskBase::DebugRecordAbilityTaskCreatedByAbility(ThisAbility);
		MyObj->InstanceName = InstanceName;
		return MyObj;
	}
	

	// Called when client requests to progress task. Task must make sure this is handled properly/securely
	virtual void ProgressTask(FInstancedStruct& TaskData){};
	
	// Client calling to progress the task forward
	// Task must make sure this is handled properly
	virtual void ClientProgressTask();
	
	void Heartbeat(){LastHeartbeatReceivedTime = AbilitySystemComponent->ActionTimer;};

protected:
	bool bTaskCompleted;

	/** Task Owner that created us */
	TWeakObjectPtr<AActor> TaskOwner;

private:
	void ClientHeartbeat() const;

	// How often client sends heartbeats to server
	float HeartbeatInterval = .1f;

	// Max time between heartbeats before server cancels task
	float HeartbeatMaxInterval = .5f;
	
	float ClientLastHeartbeatSentTime;
	float LastHeartbeatReceivedTime;
};
