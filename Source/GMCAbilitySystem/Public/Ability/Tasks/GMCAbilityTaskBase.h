#pragma once
#include "StructUtils/InstancedStruct.h"
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

	// Diagnostic accessors for the ability-cut instrumentation ([AbilityCut]/[TaskDiag] logs).
	// Expose internal liveness state so UGMCAbility can dump every task when an ability dies
	// abnormally. Read-only; no behavior impact.
	bool IsTaskCompleted() const { return bTaskCompleted; }
	int32 GetHeartbeatReceivedCount() const { return HeartbeatReceivedCount; }
	double GetLastHeartbeatReceivedTime() const { return LastHeartbeatReceivedTime; }
	double GetClientLastHeartbeatSentTime() const { return ClientLastHeartbeatSentTime; }

protected:
	bool bTaskCompleted;

	// Every end path (EndTask, EndTaskGMAS, watchdog kill, TaskOwnerEnded, ExternalCancel)
	// funnels through OnDestroy exactly once — the single safe place to unregister from the
	// owning ability's RunningTasks so a finished task never lingers there as a ghost that
	// still ticks, heartbeats or watchdogs.
	virtual void OnDestroy(bool bInOwnerFinished) override;

	/** Task Owner that created us */
	TWeakObjectPtr<AActor> TaskOwner;

	// Whether this task is running on a client or a client on a listen server
	bool IsClientOrRemoteListenServerPawn() const;

private:
	// How often the client sends heartbeats to the server, in real seconds.
	double HeartbeatInterval = 1.0;

	// Max real-time gap between heartbeats before the server cancels the task.
	// Measured against a monotonic real-time clock (FPlatformTime::Seconds), NOT the GMC
	// ActionTimer: the liveness watchdog must not be distorted by replay rewinds, time
	// dilation or game-thread hitches — those make the gameplay clock a poor proxy for the
	// wall-clock liveness this check actually wants. 3s tuned for harsh network conditions
	// (was 0.3f originally).
	double HeartbeatMaxInterval = 3.0;

	// Count of heartbeats the server has accepted for this task. Lets the timeout log
	// distinguish "client never sent one" (0) from "client sent then stalled" (>0).
	int32 HeartbeatReceivedCount = 0;

	// Real-time clock stamps (FPlatformTime::Seconds) — monotonic and replay-immune.
	// Client and server each compare only against their own clock, never across machines.
	double ClientLastHeartbeatSentTime = 0.0;
	double LastHeartbeatReceivedTime = 0.0;


};
