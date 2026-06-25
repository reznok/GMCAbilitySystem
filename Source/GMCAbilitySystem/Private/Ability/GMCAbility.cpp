#include "Ability/GMCAbility.h"
#include "GMCAbilitySystem.h"
#include "GMCPawn.h"
#include "Ability/Tasks/GMCAbilityTaskBase.h"
#include "Components/GMCAbilityComponent.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace GMCAbilityCutDiag
{
	static const TCHAR* TaskStateToString(const EGameplayTaskState State)
	{
		switch (State)
		{
		case EGameplayTaskState::Uninitialized:      return TEXT("Uninitialized");
		case EGameplayTaskState::AwaitingActivation: return TEXT("AwaitingActivation");
		case EGameplayTaskState::Paused:             return TEXT("Paused");
		case EGameplayTaskState::Active:             return TEXT("Active");
		case EGameplayTaskState::Finished:           return TEXT("Finished");
		default:                                     return TEXT("Unknown");
		}
	}
}

FString UGMCAbility::GetAbilityCutDiagnostics() const
{
	const double Now = FPlatformTime::Seconds();
	const bool bAuthority = OwnerAbilityComponent && OwnerAbilityComponent->HasAuthority();
	const bool bReplaying = OwnerAbilityComponent && OwnerAbilityComponent->IsReplayingForGMASLogic();
	const float Timer = OwnerAbilityComponent ? OwnerAbilityComponent->ActionTimer : -1.f;

	FString Out = FString::Printf(
		TEXT("Ability=%s Tag=%s AbilityID=%d State=%s ServerConfirmed=%d MovementTick=%d Authority=%d Replaying=%d ActionTimer=%.3f ClientStartTime=%.3f Age=%.3f Tasks=%d"),
		*GetName(), *AbilityTag.ToString(), AbilityID, *EnumToString(AbilityState),
		bServerConfirmed ? 1 : 0, bActivateOnMovementTick ? 1 : 0, bAuthority ? 1 : 0, bReplaying ? 1 : 0,
		Timer, ClientStartTime, Timer - ClientStartTime, RunningTasks.Num());

	for (const TPair<int, UGMCAbilityTaskBase*>& TaskPair : RunningTasks)
	{
		const UGMCAbilityTaskBase* Task = TaskPair.Value;
		if (!Task)
		{
			Out += FString::Printf(TEXT("\n  - TaskID=%d <null>"), TaskPair.Key);
			continue;
		}

		// Heartbeat stamps are wall-clock (FPlatformTime) — ages can be negative right after
		// Activate because the received-stamp is seeded one grace interval into the future.
		Out += FString::Printf(
			TEXT("\n  - TaskID=%d Class=%s State=%s Completed=%d HeartbeatsRcv=%d LastRcvAge=%.2fs LastSentAge=%.2fs"),
			TaskPair.Key, *Task->GetClass()->GetName(),
			GMCAbilityCutDiag::TaskStateToString(Task->GetState()),
			Task->IsTaskCompleted() ? 1 : 0,
			Task->GetHeartbeatReceivedCount(),
			Now - Task->GetLastHeartbeatReceivedTime(),
			Task->GetClientLastHeartbeatSentTime() > 0.0 ? Now - Task->GetClientLastHeartbeatSentTime() : -1.0);
	}

	return Out;
}

UWorld* UGMCAbility::GetWorld() const
{
	if (HasAllFlags(RF_ClassDefaultObject))
	{
		// If we're a CDO, we *must* return nullptr to avoid causing issues with
		// UObject::ImplementsGetWorld(), which just blithely and blindly calls GetWorld().
		return nullptr;
	}

#if WITH_EDITOR
	if (GIsEditor)
	{
		return GWorld;
	}
#endif // WITH_EDITOR

	// Sanity check rather than blindly accessing the world context array.
	auto Contexts = GEngine->GetWorldContexts();
	if (Contexts.Num() == 0)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("%s: instanciated class with no valid world!"), *GetClass()->GetName())
			return nullptr;
	}

	return Contexts[0].World();
}

bool UGMCAbility::IsActive() const
{
	return AbilityState != EAbilityState::PreExecution && AbilityState != EAbilityState::Ended;
}

void UGMCAbility::Tick(float DeltaTime)
{
	// Per-ability profiling scope, named by gameplay tag (class-name fallback). The FString
	// is only built in trace-enabled configs — TRACE_CPUPROFILER_EVENT_SCOPE_TEXT compiles to
	// nothing (and its argument is not evaluated) when CPUPROFILERTRACE_ENABLED == 0 (Shipping).
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("Ability::Tick [%s]"),
		AbilityTag.IsValid() ? *AbilityTag.ToString() : *GetClass()->GetName()));

	// Don't tick before the ability is initialized or after it has ended
	if (AbilityState == EAbilityState::PreExecution || AbilityState == EAbilityState::Ended) return;

	if (!OwnerAbilityComponent->HasAuthority())
	{
		if (!bServerConfirmed && ClientStartTime + ServerConfirmTimeout < OwnerAbilityComponent->ActionTimer)
		{
			// [AbilityCut] probe: the server never confirmed this AbilityID within the timeout.
			// Either the server rejected/never ran the activation, or client/server generated
			// diverging AbilityIDs and the confirm RPC targeted an instance we don't have.
			// Full task dump so the log shows what the prediction was doing when it died.
			UE_LOG(LogGMCAbilitySystem, Error,
				TEXT("[AbilityCut] Client removing unconfirmed ability after %.2fs (no RPCConfirmAbilityActivation received). %s"),
				ServerConfirmTimeout, *GetAbilityCutDiagnostics());
			EndAbility();
			return;
		}
	}

	if (bEndPending) {
		EndAbility();
		return;
	}

	TickTasks(DeltaTime);
	// A task ending itself mid-pass (or its Completed BP) can have ended the whole ability;
	// don't fire the BP tick on a dead ability.
	if (AbilityState == EAbilityState::Ended) return;
	TickEvent(DeltaTime);
}

void UGMCAbility::AncillaryTick(float DeltaTime) {
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("Ability::AncTick [%s]"),
		AbilityTag.IsValid() ? *AbilityTag.ToString() : *GetClass()->GetName()));

	// Don't tick before the ability is initialized or after it has ended
	if (AbilityState == EAbilityState::PreExecution || AbilityState == EAbilityState::Ended) return;

	// [TaskDiag] census: with finished tasks unregistering themselves from RunningTasks, a
	// server ability whose tasks ALL ended but whose graph never calls EndAbility has zero
	// task-level liveness coverage left (the old ghost-task watchdog reaped it by accident).
	// Don't auto-kill — the false-kill cure must not become a new kill path — but make the
	// leak visible: one line once the ability has been task-less for two watchdog periods.
	// Server net modes only, remote pawns only (local server pawns never ran the watchdog).
	if (!bTasklessCensusLogged && TaskIDCounter >= 0 && OwnerAbilityComponent
		&& (OwnerAbilityComponent->GetNetMode() == NM_DedicatedServer || OwnerAbilityComponent->GetNetMode() == NM_ListenServer)
		&& OwnerAbilityComponent->GMCMovementComponent
		&& !OwnerAbilityComponent->GMCMovementComponent->IsLocallyControlledServerPawn())
	{
		bool bHasLiveTask = false;
		for (const TPair<int, UGMCAbilityTaskBase*>& TaskPair : RunningTasks)
		{
			if (TaskPair.Value && TaskPair.Value->GetState() != EGameplayTaskState::Finished)
			{
				bHasLiveTask = true;
				break;
			}
		}
		if (bHasLiveTask)
		{
			TasklessSinceTime = 0.0;
		}
		else
		{
			const double Now = FPlatformTime::Seconds();
			if (TasklessSinceTime == 0.0)
			{
				TasklessSinceTime = Now;
			}
			else if (Now - TasklessSinceTime > 6.0) // 2x the task watchdog interval
			{
				// Neutral wording on purpose: abilities ended externally (cancel-by-tag, effect
				// removal) legitimately idle task-less — this is coverage info, not an accusation.
				const FString Diag = GetAbilityCutDiagnostics();
				UE_LOG(LogGMCAbilitySystem, Warning,
					TEXT("[TaskDiag] Ability task-less for %.1fs and still active — no task-level liveness coverage (may be by design for externally-ended abilities). %s"),
					Now - TasklessSinceTime, *Diag);
				UE_LOG(LogTemp, Warning,
					TEXT("[TaskDiag] Ability task-less for %.1fs and still active — no task-level liveness coverage (may be by design for externally-ended abilities). %s"),
					Now - TasklessSinceTime, *Diag);
				bTasklessCensusLogged = true;
			}
		}
	}

	AncillaryTickTasks(DeltaTime);
	// The heartbeat watchdog inside AncillaryTickTasks can have ended the whole ability;
	// don't fire the BP tick on a dead ability.
	if (AbilityState == EAbilityState::Ended) return;
	AncillaryTickEvent(DeltaTime);
}

void UGMCAbility::AncillaryTickEvent_Implementation(float DeltaTime)
{
}

void UGMCAbility::TickEvent_Implementation(float DeltaTime)
{
}

void UGMCAbility::TickTasks(float DeltaTime)
{
	// Iterate a snapshot, never the live map: tasks end themselves mid-tick (WaitDelay & co)
	// and unregister from RunningTasks in OnDestroy, and Completed broadcasts can run BP that
	// registers NEW tasks — both mutate the map under a live range-for. The per-entry Finished
	// check covers tasks mass-ended earlier in this same pass (watchdog -> EndAbility).
	TArray<UGMCAbilityTaskBase*, TInlineAllocator<8>> TasksSnapshot;
	RunningTasks.GenerateValueArray(TasksSnapshot);
	for (UGMCAbilityTaskBase* Task : TasksSnapshot)
	{
		if (!Task || Task->GetState() == EGameplayTaskState::Finished) continue;
		Task->Tick(DeltaTime);
	}
}

void UGMCAbility::AncillaryTickTasks(float DeltaTime) {
	// Same snapshot rationale as TickTasks — the watchdog inside AncillaryTick can end the
	// whole ability (mass task unregistration) while this loop is running.
	TArray<UGMCAbilityTaskBase*, TInlineAllocator<8>> TasksSnapshot;
	RunningTasks.GenerateValueArray(TasksSnapshot);
	for (UGMCAbilityTaskBase* Task : TasksSnapshot)
	{
		if (!Task || Task->GetState() == EGameplayTaskState::Finished) continue;
		Task->AncillaryTick(DeltaTime);
	}
}

void UGMCAbility::Execute(UGMC_AbilitySystemComponent* InAbilityComponent, int InAbilityID, const UInputAction* InputAction)
{
	// TODO : Add input action tag here to avoid going by the old FGMCAbilityData struct
	this->AbilityInputAction = InputAction;
	this->AbilityID = InAbilityID;
	this->OwnerAbilityComponent = InAbilityComponent;
	this->ClientStartTime = InAbilityComponent->ActionTimer;
	PreBeginAbility();
}

bool UGMCAbility::CanAffordAbilityCost(float DeltaTime) const
{
	if (AbilityCost == nullptr || OwnerAbilityComponent == nullptr) return true;

	UGMCAbilityEffect* AbilityEffect = AbilityCost->GetDefaultObject<UGMCAbilityEffect>();
	for (FGMCAttributeModifier AttributeModifier : AbilityEffect->EffectData.Modifiers)
	{
		const FAttribute* Attribute = OwnerAbilityComponent->GetAttributeByTag(AttributeModifier.AttributeTag);
		if (Attribute == nullptr) continue;

		AttributeModifier.InitModifier(AbilityEffect, OwnerAbilityComponent->ActionTimer, -1.f, false, DeltaTime);
		if (!AttributeModifier.ResolveConditions(OwnerAbilityComponent)) continue; // a skipped cost is not a cost
		if (Attribute->Value + AttributeModifier.CalculateModifierValue(*Attribute) < 0.f)
		{
			return false;
		}
	}

	return true;
}

void UGMCAbility::CommitAbilityCostAndCooldown()
{
	CommitAbilityCost();
	CommitAbilityCooldown();
}

void UGMCAbility::CommitAbilityCooldown()
{
	if (CooldownTime <= 0.f || OwnerAbilityComponent == nullptr) return;
	OwnerAbilityComponent->SetCooldownForAbility(AbilityTag, CooldownTime);
}

void UGMCAbility::CommitAbilityCost()
{
	if (AbilityCost == nullptr || OwnerAbilityComponent == nullptr) return;

	const UGMCAbilityEffect* EffectCDO = DuplicateObject(AbilityCost->GetDefaultObject<UGMCAbilityEffect>(), this);
	FGMCAbilityEffectData EffectData = EffectCDO->EffectData;
	EffectData.OwnerAbilityComponent = OwnerAbilityComponent;
	EffectData.SourceAbilityComponent = OwnerAbilityComponent;
	AbilityCostInstance = OwnerAbilityComponent->ApplyAbilityEffect(DuplicateObject(EffectCDO, this), EffectData);
}

void UGMCAbility::RemoveAbilityCost() {
	if (AbilityCostInstance) {
		OwnerAbilityComponent->RemoveActiveAbilityEffect(AbilityCostInstance);
	}
}


void UGMCAbility::ModifyBlockOtherAbility(FGameplayTagContainer TagToAdd, FGameplayTagContainer TagToRemove) {
	for (auto Tag : TagToAdd) {
		BlockOtherAbility.AddTag(Tag);
	}

	for (auto Tag : TagToRemove) {
		BlockOtherAbility.RemoveTag(Tag);
	}
}


void UGMCAbility::ResetBlockOtherAbility() {
	BlockOtherAbility = GetClass()->GetDefaultObject<UGMCAbility>()->BlockOtherAbility;
}


void UGMCAbility::HandleTaskData(int TaskID, FInstancedStruct TaskData)
{
	const FGMCAbilityTaskData TaskDataFromInstance = TaskData.Get<FGMCAbilityTaskData>();
	if (RunningTasks.Contains(TaskID) && RunningTasks[TaskID] != nullptr)
	{
		if (TaskDataFromInstance.TaskType == EGMCAbilityTaskDataType::Progress)
		{
			// Idempotency guard for ALL task types: Progress payloads ride a GMC-bound
			// ClientAuth_Input, so a client replay re-delivers the historical payload of every
			// replayed move. No ProgressTask implementation (SetTargetData*, WaitForInputKey*)
			// guards against re-entry on a finished task — without this gate the re-delivery
			// re-broadcasts Completed and re-runs the BP continuation (double heal/commit,
			// client-only task creation -> TaskID divergence -> heartbeat watchdog kill).
			UGMCAbilityTaskBase* Task = RunningTasks[TaskID];
			if (Task->IsTaskCompleted() || Task->GetState() == EGameplayTaskState::Finished)
			{
				UE_LOG(LogGMCAbilitySystem, Verbose,
					TEXT("[TaskDiag] Progress payload ignored for already-finished task (TaskID=%d, Replaying=%d)."),
					TaskID, OwnerAbilityComponent && OwnerAbilityComponent->IsReplayingForGMASLogic() ? 1 : 0);
				return;
			}
			Task->ProgressTask(TaskData);
		}
	}
	else if (TaskID <= TaskIDCounter)
	{
		// TaskIDs are monotonic and never reused, so an ID at or below the counter was issued
		// here and its task already ended and unregistered — usually a benign late/replayed
		// re-delivery racing the purge (the payload is correctly ignored either way). Caveat:
		// a SHIFTED-ID divergence (both sides issued this numeric ID for different logical
		// tasks) is indistinguishable here — don't rule divergence out on this line alone.
		UE_LOG(LogGMCAbilitySystem, Verbose,
			TEXT("[TaskDiag] Progress payload for already-unregistered TaskID=%d ignored (benign end race, Replaying=%d)."),
			TaskID, OwnerAbilityComponent && OwnerAbilityComponent->IsReplayingForGMASLogic() ? 1 : 0);
	}
	else
	{
		// [TaskDiag] probe: a Progress payload arrived for a TaskID this side NEVER issued
		// (above our monotonic counter). This is the silent dispatch failure that leaves the
		// other side's task waiting forever — TaskIDs are independent per-side counters, so
		// any asymmetric task creation (e.g. a BP branch firing on one side only, or a replay
		// double-executing a graph) shifts every subsequent ID.
		const FString Diag = GetAbilityCutDiagnostics();
		UE_LOG(LogGMCAbilitySystem, Warning,
			TEXT("[TaskDiag] Progress payload dropped: TaskID=%d never issued on this side (max issued %d) — TaskID divergence. %s"),
			TaskID, TaskIDCounter, *Diag);
		if (OwnerAbilityComponent && OwnerAbilityComponent->HasAuthority())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[TaskDiag] Progress payload dropped: TaskID=%d never issued on this side (max issued %d) — TaskID divergence. %s"),
				TaskID, TaskIDCounter, *Diag);
		}
	}
}

void UGMCAbility::HandleTaskHeartbeat(int TaskID)
{
	if (RunningTasks.Contains(TaskID) && RunningTasks[TaskID] != nullptr) // Do we ever remove orphans tasks ?
	{
		RunningTasks[TaskID]->Heartbeat();
	}
	else if (TaskID <= TaskIDCounter)
	{
		// Heartbeat for a task we issued and already ended/unregistered — the sender's twin
		// just hasn't ended yet (up to one-way transit + the 1s send cadence). Benign.
		UE_LOG(LogGMCAbilitySystem, Verbose,
			TEXT("[TaskDiag] Heartbeat for already-unregistered TaskID=%d ignored (benign end race)."), TaskID);
	}
	else if (!WarnedDivergentTaskIDs.Contains(TaskID))
	{
		// [TaskDiag] probe: the sender is heartbeating a TaskID this side NEVER issued (above
		// our monotonic counter) — its task layout diverged from ours. If a real twin was
		// expected here it is starving and the watchdog will cancel the ability; an APPENDED
		// extra task (e.g. created client-side during replay) starves nothing and just keeps
		// beating. Warn once per TaskID — repeats at the 1/s send rate go Verbose below.
		WarnedDivergentTaskIDs.Add(TaskID);
		const FString Diag = GetAbilityCutDiagnostics();
		UE_LOG(LogGMCAbilitySystem, Warning,
			TEXT("[TaskDiag] Heartbeat for TaskID=%d never issued on this side (max issued %d) — TaskID divergence. %s"),
			TaskID, TaskIDCounter, *Diag);
		if (OwnerAbilityComponent && OwnerAbilityComponent->HasAuthority())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[TaskDiag] Heartbeat for TaskID=%d never issued on this side (max issued %d) — TaskID divergence. %s"),
				TaskID, TaskIDCounter, *Diag);
		}
	}
	else
	{
		UE_LOG(LogGMCAbilitySystem, Verbose,
			TEXT("[TaskDiag] Heartbeat for divergent TaskID=%d (already warned)."), TaskID);
	}
}

void UGMCAbility::CancelConflictingAbilities()
{
	for (const auto& AbilityToCancelTag : CancelAbilitiesWithTag) {
		if (AbilityTag == AbilityToCancelTag) {
			UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Ability (tag) %s is trying to cancel itself, if you attempt to reset the ability, please use //TODO instead"), *AbilityTag.ToString());
			continue;
		}

		if (OwnerAbilityComponent->EndAbilitiesByTag(AbilityToCancelTag)) {
			UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability (tag) %s has been cancelled by (tag) %s"), *AbilityTag.ToString(), *AbilityToCancelTag.ToString());
		}
	}

	if (!EndOtherAbilitiesQuery.IsEmpty())
	{
		for (const auto& ActiveAbility : OwnerAbilityComponent->GetActiveAbilities())
		{
			if (ActiveAbility.Value == this) continue;

			if (EndOtherAbilitiesQuery.Matches(ActiveAbility.Value->AbilityDefinition))
			{
				ActiveAbility.Value->SetPendingEnd();
				UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability %s cancelled ability %s (matching definition query)"),
					*AbilityTag.ToString(), *ActiveAbility.Value->AbilityTag.ToString());
			}
		}
	}
}


void UGMCAbility::ServerConfirm()
{
	bServerConfirmed = true;
}


void UGMCAbility::SetPendingEnd() {
	bEndPending = true;
}


UGameplayTasksComponent* UGMCAbility::GetGameplayTasksComponent(const UGameplayTask& Task) const
{
	if (OwnerAbilityComponent != nullptr) { return OwnerAbilityComponent; }
	return nullptr;
}

AActor* UGMCAbility::GetGameplayTaskOwner(const UGameplayTask* Task) const
{
	if (OwnerAbilityComponent != nullptr) { return OwnerAbilityComponent->GetOwner(); }
	return nullptr;
}

AActor* UGMCAbility::GetGameplayTaskAvatar(const UGameplayTask* Task) const
{
	// Wtf is avatar?
	if (OwnerAbilityComponent != nullptr) { return OwnerAbilityComponent->GetOwner(); }
	return nullptr;
}

void UGMCAbility::OnGameplayTaskInitialized(UGameplayTask& Task)
{
	UGMCAbilityTaskBase* AbilityTask = Cast<UGMCAbilityTaskBase>(&Task);
	if (!AbilityTask)
	{
		// UE_LOG(LogGMCAbilitySystem, Error, TEXT("UGMCAbility::OnGameplayTaskInitialized called with non-UGMCAbilityTaskBase task"));
		return;
	}
	AbilityTask->SetAbilitySystemComponent(OwnerAbilityComponent);
	AbilityTask->Ability = this;

}

void UGMCAbility::OnGameplayTaskActivated(UGameplayTask& Task)
{
	ActiveTasks.Add(&Task);
}

void UGMCAbility::OnGameplayTaskDeactivated(UGameplayTask& Task)
{
	ActiveTasks.Remove(&Task);
}


void UGMCAbility::FinishEndAbility() {

	// [AbilityCut] probe: an ability ending while it still has unfinished tasks is the
	// fingerprint of an abnormal cut (watchdog kill, confirm timeout, cancel-by-other,
	// gameplay guard, forced server end). Normal completions end with every task already
	// completed/finished. Logged on BOTH sides: the side that dies FIRST is the root cause —
	// the other side follows seconds later (client stops heartbeating -> server watchdog,
	// or server RPCClientEndAbility -> client). Compare timestamps across the two logs.
	int32 UnfinishedTasks = 0;
	for (const TPair<int, UGMCAbilityTaskBase*>& Task : RunningTasks)
	{
		if (Task.Value && !Task.Value->IsTaskCompleted() && Task.Value->GetState() != EGameplayTaskState::Finished)
		{
			UnfinishedTasks++;
		}
	}
	if (UnfinishedTasks > 0)
	{
		const FString Diag = GetAbilityCutDiagnostics();
		UE_LOG(LogGMCAbilitySystem, Warning,
			TEXT("[AbilityCut] Ability ending with %d unfinished task(s). %s"),
			UnfinishedTasks, *Diag);
		// Mirror onto LogTemp: the dedicated-server log export only ships a fixed category
		// allowlist (LogTemp included, LogGMCAbilitySystem not).
		if (OwnerAbilityComponent && OwnerAbilityComponent->HasAuthority())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[AbilityCut] Ability ending with %d unfinished task(s). %s"),
				UnfinishedTasks, *Diag);
		}
	}

	// Snapshot: EndTaskGMAS -> EndTask -> OnDestroy unregisters the entry being visited,
	// which would invalidate a live range-for over the map. EndTask itself is idempotent
	// (engine-guarded on TaskState != Finished), so re-ending a task is a safe no-op.
	TArray<UGMCAbilityTaskBase*, TInlineAllocator<8>> TasksToEnd;
	RunningTasks.GenerateValueArray(TasksToEnd);
	for (UGMCAbilityTaskBase* Task : TasksToEnd)
	{
		if (Task == nullptr) continue;
		Task->EndTaskGMAS();
	}

	// End handled effect
	for (const auto& EfData : DeclaredEffect)
	{
		// Skip Auth effect removal on client
		if (EfData.Value == EGMCAbilityEffectQueueType::ServerAuth && !OwnerAbilityComponent->HasAuthority())  { continue;}

		if (UGMCAbilityEffect* Effect =	OwnerAbilityComponent->GetEffectById(EfData.Key))
		{
			// Don't try to close effects that are already ended
			if (Effect->CurrentState == EGMASEffectState::Started)
			{
				// Predicted's Safe path ensure-rejects outside a GMC tick. Remap to
				// PredictedQueued only when called from an RPC handler (outside any
				// tick); inside a tick, Predicted removes immediately with no delay.
				const bool bInsideGMCTick =
					(OwnerAbilityComponent->GMCMovementComponent && OwnerAbilityComponent->GMCMovementComponent->IsExecutingMove())
					|| OwnerAbilityComponent->IsInAncillaryTick()
					|| OwnerAbilityComponent->GetNetMode() == NM_Standalone;

				const EGMCAbilityEffectQueueType QueueType =
					(EfData.Value == EGMCAbilityEffectQueueType::Predicted && !bInsideGMCTick)
						? EGMCAbilityEffectQueueType::PredictedQueued
						: EfData.Value;
				OwnerAbilityComponent->RemoveActiveAbilityEffectSafe(Effect, QueueType);
			}
			else
			{
				UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Effect Handle %d already ended for ability %s"), EfData.Key, *AbilityTag.ToString());
			}
		}
	}

	// Chain hooks: apply / remove effects when this ability ends. Same queue-type detection as
	// the DeclaredEffect removal block above — Predicted requires being inside a GMC tick or
	// Standalone, otherwise PredictedQueued is used to defer until the next safe window.
	if (OwnerAbilityComponent && (ApplyEffectOnEnd.Num() > 0 || !RemoveEffectOnEnd.IsEmpty()))
	{
		const bool bInsideGMCTick =
			(OwnerAbilityComponent->GMCMovementComponent && OwnerAbilityComponent->GMCMovementComponent->IsExecutingMove())
			|| OwnerAbilityComponent->IsInAncillaryTick()
			|| OwnerAbilityComponent->GetNetMode() == NM_Standalone;
		const EGMCAbilityEffectQueueType ChainQueueType =
			bInsideGMCTick ? EGMCAbilityEffectQueueType::Predicted : EGMCAbilityEffectQueueType::PredictedQueued;

		for (const TSubclassOf<UGMCAbilityEffect>& EffectClass : ApplyEffectOnEnd)
		{
			if (EffectClass)
			{
				OwnerAbilityComponent->ApplyAbilityEffectShort(EffectClass, ChainQueueType);
			}
		}

		for (const FGameplayTag& EffectTag : RemoveEffectOnEnd)
		{
			if (EffectTag.IsValid())
			{
				OwnerAbilityComponent->RemoveEffectByTagSafe(EffectTag, -1, ChainQueueType);
			}
		}
	}

	AbilityState = EAbilityState::Ended;
}


bool UGMCAbility::IsOnCooldown() const
{
	return OwnerAbilityComponent->GetCooldownForAbility(AbilityTag) > 0;
}


bool UGMCAbility::PreExecuteCheckEvent_Implementation() {
	return true;
}


void UGMCAbility::DeclareEffect(int OutEffectHandle, EGMCAbilityEffectQueueType EffectType)
{
	if (DeclaredEffect.Contains(OutEffectHandle))
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("Effect Handle %d already declared for ability %s"), OutEffectHandle, *AbilityTag.ToString());
		return;
	}
	DeclaredEffect.Add(OutEffectHandle, EffectType);
}

bool UGMCAbility::PreBeginAbility()
{
	if (IsOnCooldown())
	{
		UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability Activation for %s Stopped By Cooldown"), *AbilityTag.ToString());
		CancelAbility();
		return false;
	}

	// PreCheck
	if (!PreExecuteCheckEvent())
	{
		UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability Activation for %s Stopped By Failing PreExecution check"), *AbilityTag.ToString());
		CancelAbility();
		return false;
	}


	TArray<UGMCAbility*> ActiveAbilities;
	OwnerAbilityComponent->GetActiveAbilities().GenerateValueArray(ActiveAbilities);

	for (auto& OtherAbilityTag : BlockedByOtherAbility)
	{
		if (ActiveAbilities.FindByPredicate([&OtherAbilityTag](const UGMCAbility* ActiveAbility) {
			return ActiveAbility
			&& ActiveAbility->IsActive()
			&& ActiveAbility->AbilityTag.MatchesTag(OtherAbilityTag);
		}))
		{
			UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability Activation for %s Stopped because Blocked By Other Ability (%s)"), *AbilityTag.ToString(), *OtherAbilityTag.ToString());
			CancelAbility();
			return false;
		}
	}


	if (OwnerAbilityComponent->IsAbilityTagBlocked(AbilityTag)) {
		UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability Activation for %s Stopped because Blocked By Other Ability"), *AbilityTag.ToString());
		CancelAbility();
		return false;
	}


	BeginAbility();

	return true;
}


void UGMCAbility::BeginAbility()
{


	OwnerAbilityComponent->OnAbilityActivated.Broadcast(this, AbilityTag);

	if (!BlockOtherAbilitiesQuery.IsEmpty())
	{
		FGameplayTagQuery BlockQuery = BlockOtherAbilitiesQuery;
		for (auto& ActiveAbility : OwnerAbilityComponent->GetActiveAbilities())
		{
			const FGameplayTagContainer& ActiveAbilityTags = ActiveAbility.Value->AbilityDefinition;

			if (BlockQuery.Matches(ActiveAbilityTags))
			{
				ActiveAbility.Value->SetPendingEnd();
				UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability %s blocked ability %s (matching query)"),
					*AbilityTag.ToString(), *ActiveAbility.Value->AbilityTag.ToString());
			}
		}
	}

	if (bApplyCooldownAtAbilityBegin)
	{
		CommitAbilityCooldown();
	}

	// Initialize Ability
	AbilityState = EAbilityState::Initialized;

	// Cancel Abilities in CancelAbilitiesWithTag container
	CancelConflictingAbilities();

	// Execute BP Event
	BeginAbilityEvent();
}

void UGMCAbility::BeginAbilityEvent_Implementation()
{
}

void UGMCAbility::EndAbility()
{
	if (AbilityState != EAbilityState::Ended) {
		FinishEndAbility();
		EndAbilityEvent();
		OwnerAbilityComponent->OnAbilityEnded.Broadcast(this);
	}
}


void UGMCAbility::CancelAbility() {
	if (AbilityState != EAbilityState::Ended) {
		FinishEndAbility();
	}
}

void UGMCAbility::EndAbilityEvent_Implementation()
{
}

AActor* UGMCAbility::GetOwnerActor() const
{
	return OwnerAbilityComponent->GetOwner();
}

AGMC_Pawn* UGMCAbility::GetOwnerPawn() const {
	if (AGMC_Pawn* OwningPawn = Cast<AGMC_Pawn>(GetOwnerActor())) {
		return OwningPawn;
	}
	return nullptr;
}

AGMC_PlayerController* UGMCAbility::GetOwningPlayerController() const {
	if (const AGMC_Pawn* OwningPawn = GetOwnerPawn()) {
		if (AGMC_PlayerController* OwningPC = Cast<AGMC_PlayerController>(OwningPawn->GetController())) {
			return OwningPC;
		}
	}
	return nullptr;
}

float UGMCAbility::GetOwnerAttributeValueByTag(FGameplayTag AttributeTag) const
{
	return OwnerAbilityComponent->GetAttributeValueByTag(AttributeTag);
}


void UGMCAbility::SetOwnerJustTeleported(bool bValue)
{
	OwnerAbilityComponent->bJustTeleported = bValue;
}

void UGMCAbility::ModifyBlockOtherAbilitiesViaDefinitionQuery(const FGameplayTagQuery& NewQuery)
{
	BlockOtherAbilitiesQuery = NewQuery;
	UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("BlockOtherAbilityByDefinitionQuery modified: %s"), *NewQuery.GetDescription());
}
