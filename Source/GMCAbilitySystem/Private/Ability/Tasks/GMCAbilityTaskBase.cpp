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

	// [TaskDiag] probe: a task registered DURING a client replay is created on the client
	// ONLY (the server never replays), so the per-ability TaskIDCounter diverges from here
	// on — every later task on this ability gets mismatched IDs, Progress payloads dispatch
	// to the wrong/missing task and heartbeats starve the server twin until its watchdog
	// cancels the ability. This is the smoking gun for replay-induced cuts.
	if (AbilitySystemComponent.IsValid() && AbilitySystemComponent->IsReplayingForGMASLogic())
	{
		UE_LOG(LogGMCAbilitySystem, Error,
			TEXT("[TaskDiag] Task %s registered DURING replay (TaskID=%d) — client/server TaskID divergence from this point. %s"),
			*GetClass()->GetName(), TaskID, Ability ? *Ability->GetAbilityCutDiagnostics() : TEXT("<no ability>"));
	}
}

void UGMCAbilityTaskBase::EndTaskGMAS()
{
	EndTask();
}

void UGMCAbilityTaskBase::OnDestroy(bool bInOwnerFinished)
{
	// Unregister BEFORE Super (which marks this object garbage). The value check is mandatory:
	// a never-activated task still carries the zero-init TaskID of 0, which aliases the first
	// real registered task — a bare Remove(TaskID) would evict that live task and silence its
	// heartbeats, recreating the very starvation bug this purge exists to fix.
	if (Ability && Ability->RunningTasks.FindRef(TaskID) == this)
	{
		Ability->RunningTasks.Remove(TaskID);
	}
	Super::OnDestroy(bInOwnerFinished);
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
	// AbilitySystemComponent is a TWeakObjectPtr: during pawn teardown / possession change the
	// component can die while this task is still registered, and a stale dereference crashes.
	if (!AbilitySystemComponent.IsValid() || !AbilitySystemComponent->GMCMovementComponent) return;

	// A task that already ended must neither heartbeat nor watchdog: its twin's lifetime is no
	// longer tied to ours. Without this, a Finished-but-still-registered server task keeps its
	// watchdog armed while the destroyed client twin no longer heartbeats — the watchdog then
	// kills a healthy ability on a pure GC-timing race (proven heal-cut bug, 2026-06-12).
	if (GetState() == EGameplayTaskState::Finished || bTaskCompleted) return;

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
		// Full task dump appended: the timed-out task is merely the FIRST one to starve —
		// when the client stops heartbeating, every task starves at once, so what matters is
		// the whole-ability picture (which tasks had completed, which never progressed,
		// heartbeat counts per task) plus whether the timed-out task was even still pending.
		UE_LOG(LogTemp, Error, TEXT("[TaskHeartbeat] Timeout: cancelling ability '%s' (tag '%s') - task %s (TaskID %d, Completed=%d), %.2fs since last heartbeat (max %.2f), %d heartbeats received. %s"),
		  *Ability->GetName(), *Ability->AbilityTag.ToString(), *GetClass()->GetName(), TaskID,
		  bTaskCompleted ? 1 : 0,
		  TimeSinceLastHeartbeat, HeartbeatMaxInterval, HeartbeatReceivedCount,
		  *Ability->GetAbilityCutDiagnostics());
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
	// Null-safe: derived AncillaryTick bodies keep executing after the base early-returns on a
	// dead component, so this helper must not assume the weak-ptr guard already passed.
	if (!AbilitySystemComponent.IsValid() || !AbilitySystemComponent->GMCMovementComponent) return false;

	return (AbilitySystemComponent->GetNetMode() != NM_DedicatedServer &&
		AbilitySystemComponent->GetNetMode() != NM_ListenServer) ||
		AbilitySystemComponent->GMCMovementComponent->IsLocallyControlledServerPawn();
}
