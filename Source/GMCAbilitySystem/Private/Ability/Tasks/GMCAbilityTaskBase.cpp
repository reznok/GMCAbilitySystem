#include "Ability/Tasks/GMCAbilityTaskBase.h"

#include "HAL/PlatformTime.h"
#include "GMCAbilityComponent.h"
#include "Ability/GMCAbility.h"



void UGMCAbilityTaskBase::Activate()
{
	Super::Activate();
	RegisterTask(this);
	// Seed with a real-time stamp plus one full interval of grace, so a task that activates
	// just before its first heartbeat round-trip completes is not cancelled prematurely.
	LastHeartbeatReceivedTime = FPlatformTime::Seconds() + HeartbeatMaxInterval;
}

void UGMCAbilityTaskBase::EndTaskGMAS()
{
	EndTask();
}

void UGMCAbilityTaskBase::SetAbilitySystemComponent(UGMC_AbilitySystemComponent* InAbilitySystemComponent)
{
	this->AbilitySystemComponent = InAbilitySystemComponent;
}

void UGMCAbilityTaskBase::RegisterTask(UGMCAbilityTaskBase* Task)
{
	TaskID = Ability->GetNextTaskID();
	Ability->RegisterTask(TaskID, Task);
}

void UGMCAbilityTaskBase::Tick(float DeltaTime)
{

}

void UGMCAbilityTaskBase::AncillaryTick(float DeltaTime){
	// Locally controlled server pawns don't need to send heartbeats
	if (AbilitySystemComponent->GMCMovementComponent->IsLocallyControlledServerPawn()) return;

	// Monotonic real-time clock. Deliberately NOT the GMC ActionTimer: the heartbeat
	// watchdog measures wall-clock liveness, so it must stay immune to replay rewinds,
	// time dilation and the ActionTimer advancing at a different rate than real time.
	const double Now = FPlatformTime::Seconds();

	// If not the server version of the component, send heartbeats
	if (AbilitySystemComponent->GetNetMode() != NM_DedicatedServer &&
		AbilitySystemComponent->GetNetMode() != NM_ListenServer)
	{
		if (ClientLastHeartbeatSentTime + HeartbeatInterval < Now)
		{
			AbilitySystemComponent->RPCTaskHeartbeat(Ability->GetAbilityID(), TaskID);
			ClientLastHeartbeatSentTime = Now;
		}
	}
	else if (LastHeartbeatReceivedTime + HeartbeatMaxInterval < Now)
	{
		const double TimeSinceLastHeartbeat = Now - LastHeartbeatReceivedTime;
		UE_LOG(LogGMCReplication, Error, TEXT("Server Task Heartbeat Timeout after %.2fs (max: %.2f), Cancelling Ability: %s"),
		  TimeSinceLastHeartbeat, HeartbeatMaxInterval, *Ability->GetName());
		// Mirror onto LogTemp: the dedicated-server GS log export only ships a fixed category
		// allowlist (LogTemp included, LogGMCReplication not), so a LogGMCReplication-only line
		// is invisible in server log dumps. This watchdog is what silently cancels heal-consume.
		UE_LOG(LogTemp, Error, TEXT("[TaskHeartbeat] Timeout: cancelling ability '%s' (tag '%s') - task %s (TaskID %d), %.2fs since last heartbeat (max %.2f), %d heartbeats received"),
		  *Ability->GetName(), *Ability->AbilityTag.ToString(), *GetClass()->GetName(), TaskID,
		  TimeSinceLastHeartbeat, HeartbeatMaxInterval, HeartbeatReceivedCount);
		AbilitySystemComponent->OnTaskTimeout.Broadcast(Ability->AbilityTag);
		Ability->EndAbility();
		EndTask();
	}
}

void UGMCAbilityTaskBase::ClientProgressTask()
{
	FGMCAbilityTaskData TaskData;
	TaskData.TaskType = EGMCAbilityTaskDataType::Progress;
	TaskData.AbilityID = Ability->GetAbilityID();
	TaskData.TaskID = TaskID;
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(TaskData);
	Ability->OwnerAbilityComponent->QueueTaskData(TaskDataInstance);
}

void UGMCAbilityTaskBase::Heartbeat()
{
	// Real-time stamp: monotonic and replay-immune, so the SV_SwapServerState() pair that
	// used to bracket this read is no longer needed — it only existed to read a coherent
	// server-authoritative ActionTimer, which this watchdog deliberately no longer uses.
	LastHeartbeatReceivedTime = FPlatformTime::Seconds();
	HeartbeatReceivedCount++;
}

bool UGMCAbilityTaskBase::IsClientOrRemoteListenServerPawn() const
{
	return (AbilitySystemComponent->GetNetMode() != NM_DedicatedServer &&
		AbilitySystemComponent->GetNetMode() != NM_ListenServer) ||
		AbilitySystemComponent->GMCMovementComponent->IsLocallyControlledServerPawn();
}
