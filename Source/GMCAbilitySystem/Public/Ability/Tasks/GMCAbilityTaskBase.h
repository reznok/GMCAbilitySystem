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

	// An overridable function for when the task is being ended by the ability system
	// Allows for any cleanup that may be required if an ability is force ended
	virtual void EndTaskGMAS();
	
	void SetAbilitySystemComponent(UGMC_AbilitySystemComponent* InAbilitySystemComponent);
	void RegisterTask(UGMCAbilityTaskBase* Task);

	/** GameplayAbility that created us */
	UPROPERTY()
	UGMCAbility* Ability;

	UPROPERTY()
	TWeakObjectPtr<UGMC_AbilitySystemComponent> AbilitySystemComponent;

	// Tick called by AbilityComponent, different from TickTask
	virtual void Tick(float DeltaTime);

	// AncillaryTick called by AbilityComponent, different from TickTask
	virtual void AncillaryTick(float DeltaTime);

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
	
	virtual void Heartbeat();

protected:
	bool bTaskCompleted;

	/** Task Owner that created us */
	TWeakObjectPtr<AActor> TaskOwner;

	// Whether this task is running on a client or a client on a listen server
	bool IsClientOrRemoteListenServerPawn() const;

private:
	// How often client sends heartbeats to server
	float HeartbeatInterval = .2f;

	// Max time between heartbeats before server cancels task
	// Aherys: previous value was 0.3f it's maybe a bit too low for harsh network conditions
	float HeartbeatMaxInterval = 3.f;
	
	float ClientLastHeartbeatSentTime = 0.f;
	float LastHeartbeatReceivedTime = 0.f;


};
