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
	const double Timer = OwnerAbilityComponent ? OwnerAbilityComponent->ActionTimer : -1.0;
	const double ConfirmNow = OwnerAbilityComponent ? OwnerAbilityComponent->GetConfirmClock() : -1.0;

	FString Out = FString::Printf(
		TEXT("Ability=%s Tag=%s AbilityID=%d State=%s ServerConfirmed=%d MovementTick=%d Authority=%d Replaying=%d ActionTimer=%.3f ClientStartTime=%.3f Age=%.3f ConfirmClock=%.3f ConfirmAge=%.3f Tasks=%d"),
		*GetName(), *AbilityTag.ToString(), AbilityID, *EnumToString(AbilityState),
		bServerConfirmed ? 1 : 0, bActivateOnMovementTick ? 1 : 0, bAuthority ? 1 : 0, bReplaying ? 1 : 0,
		Timer, ClientStartTime, Timer - ClientStartTime, ConfirmNow, ConfirmNow - ClientConfirmStartTime, RunningTasks.Num());

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
	// is only built while the CPU trace channel is on; TRACE_CPUPROFILER_EVENT_SCOPE_TEXT compiles
	// to nothing (and its argument is not evaluated) when CPUPROFILERTRACE_ENABLED == 0 (Shipping).
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*(UE_TRACE_CHANNELEXPR_IS_ENABLED(CpuChannel)
		? FString::Printf(TEXT("Ability::Tick [%s]"), AbilityTag.IsValid() ? *AbilityTag.ToString() : *GetClass()->GetName())
		: FString()));

	// Don't tick before the ability is initialized or after it has ended
	if (AbilityState == EAbilityState::PreExecution || AbilityState == EAbilityState::Ended) return;

	if (!OwnerAbilityComponent->IsAuthorityForGMASLogic())
	{
		// Timed on the confirm clock, not ActionTimer: ActionTimer is the move timestamp and jumps to
		// the server's clock when a joining client's first moves are acknowledged, which would read
		// as an age of thousands of seconds and cancel a healthy prediction.
		// The last resort of ability sync: the server answers every covered activation (Confirmed or
		// Rejected), so only an activation the server never processed (a lost operation) reaches this.
		// Client-authorized instances are never answered by a confirm and never time out.
		FGMASCoveredAbility Covered;
		Covered.bConfirmed = bServerConfirmed;
		Covered.bClientAuthorized = bClientAuthorized;
		Covered.StartConfirmClock = ClientConfirmStartTime;
		if (GMASAbilitySyncRules::ShouldTimeOut(Covered, OwnerAbilityComponent->GetConfirmClock(), ServerConfirmTimeout))
		{
			// [AbilityCut] probe: no answer for this activation within the timeout. The server never
			// ran the activation (its operation was lost), or the answer was lost. Full task dump so
			// the log shows what the prediction was doing when it died.
			UE_LOG(LogGMCAbilitySystem, Error,
				TEXT("[AbilityCut] Client cancelling unconfirmed ability after %.2fs (no ability answer received). %s"),
				ServerConfirmTimeout, *GetAbilityCutDiagnostics());
			CancelAbility();
			return;
		}
	}

	if (bCancelPending) { CancelAbility(); return; }
	if (bEndPending) { EndAbility(); return; }

	TickTasks(DeltaTime);
	// A task ending itself mid-pass (or its Completed BP) can have ended the whole ability;
	// don't fire the BP tick on a dead ability.
	if (AbilityState == EAbilityState::Ended) return;
	TickEvent(DeltaTime);
}

void UGMCAbility::AncillaryTick(float DeltaTime) {
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*(UE_TRACE_CHANNELEXPR_IS_ENABLED(CpuChannel)
		? FString::Printf(TEXT("Ability::AncTick [%s]"), AbilityTag.IsValid() ? *AbilityTag.ToString() : *GetClass()->GetName())
		: FString()));

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
				UE_LOG(LogGMCAbilitySystem, Verbose,
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
	this->ClientConfirmStartTime = InAbilityComponent->GetConfirmClock();
	PreBeginAbility();
}

namespace
{
	// Ability classes (full path names) already warned about a cost attribute that is not GMC-bound:
	// once per class per process.
	TSet<FString> GWarnedUnboundCostAbilityClasses;
}

void UGMCAbility::WarnUnboundCostAttributes() const
{
	if (!CostQueryEffect || !OwnerAbilityComponent) { return; }
	const FString ClassPath = GetClass()->GetPathName();
	if (GWarnedUnboundCostAbilityClasses.Contains(ClassPath)) { return; }

	// The activation gate reads these values on both sides; only a GMC-bound attribute is guaranteed
	// to hold the same value on the client and the server at the same move.
	TArray<FString> Unbound;
	auto Check = [this, &Unbound](const FGameplayTag& Tag)
	{
		const FAttribute* Attribute = OwnerAbilityComponent->GetAttributeByTag(Tag);
		if (Attribute && !Attribute->bIsGMCBound) { Unbound.AddUnique(Tag.ToString()); }
	};
	for (const FGMCAttributeModifier& Modifier : CostQueryEffect->EffectData.Modifiers)
	{
		Check(Modifier.AttributeTag);
		Check(Modifier.ValueAsAttribute);
	}
	if (Unbound.Num() == 0) { return; }

	GWarnedUnboundCostAbilityClasses.Add(ClassPath);
	UE_LOG(LogGMCAbilitySystem, Warning, TEXT("%s: cost attribute %s is not GMC-bound; client and server may disagree on affordability. Reported once per ability class."),
		*GetClass()->GetName(), *FString::Join(Unbound, TEXT(", ")));
}

UGMCAbilityEffect* UGMCAbility::GetCostQueryEffect()
{
	if (!AbilityCost || !OwnerAbilityComponent) { return nullptr; }

	// Built once and reused: the readers copy each modifier by value before InitModifier /
	// ResolveConditions, so the cached effect's data stays pristine. Rebuilt if AbilityCost changed
	// under it; re-wired if the owner did.
	if (!CostQueryEffect || CostQueryEffect->GetClass() != AbilityCost.Get())
	{
		CostQueryEffect = DuplicateObject(AbilityCost->GetDefaultObject<UGMCAbilityEffect>(), this);
	}
	if (CostQueryEffect->GetOwnerAbilityComponent() != OwnerAbilityComponent)
	{
		CostQueryEffect->InitializeForQuery(OwnerAbilityComponent);
		WarnUnboundCostAttributes();
	}
	return CostQueryEffect;
}

TMap<FGameplayTag, float> UGMCAbility::ProjectAbilityCost(float DeltaTime) const
{
	// The modifiers are evaluated through a query effect wired to the owner: an attribute-sourced
	// value (AMT_Attribute, AddPercentageAttribute, ...) read through the bare CDO has no owner and
	// contributes 0, which made such a cost always affordable. Per attribute, start from the current
	// Value and walk the resolved modifiers in order the way the permanent apply path does.
	// The projection reads Value before this move's pending Instant costs are processed, so two
	// activations inside one move can both pass the gate on the same Value. That is deterministic on
	// both sides (same move, same Value), so it never diverges; it only lets such a pair overdraw.
	TMap<FGameplayTag, float> Projected;
	// The cache is a lazily built transient object; the projection itself is read-only.
	UGMCAbilityEffect* Query = const_cast<UGMCAbility*>(this)->GetCostQueryEffect();
	if (!Query) return Projected;

	for (FGMCAttributeModifier Modifier : Query->EffectData.Modifiers)
	{
		const FAttribute* Attribute = OwnerAbilityComponent->GetAttributeByTag(Modifier.AttributeTag);
		if (Attribute == nullptr) continue;

		Modifier.InitModifier(Query, OwnerAbilityComponent->ActionTimer, -1, false, DeltaTime);
		if (!Modifier.ResolveConditions(OwnerAbilityComponent)) continue; // a skipped cost is not a cost

		float* Found = Projected.Find(Modifier.AttributeTag);
		float& Value = Found ? *Found : Projected.Add(Modifier.AttributeTag, Attribute->Value);
		const float Delta = Modifier.CalculateModifierValue(*Attribute);
		if (Modifier.Op == EModifierType::Set || Modifier.Op == EModifierType::SetReplace)
		{
			Value = Delta;            // the absolute target for these ops
		}
		else if (Modifier.Op == EModifierType::AddPercentageOfBase)
		{
			// Scales the projected value: an approximation of the real apply, which scales the base layer
			// (the two agree while no temporary modifier sits on the attribute).
			Value *= 1.f + Delta;
		}
		else
		{
			Value += Delta;
		}
	}
	return Projected;
}

bool UGMCAbility::CanAffordAbilityCost(float DeltaTime) const
{
	// Judged per attribute over every modifier of the cost: two drains on one attribute add up and
	// a Set is absolute.
	for (const TPair<FGameplayTag, float>& Projected : ProjectAbilityCost(DeltaTime))
	{
		if (Projected.Value < 0.f)
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

	UGMCAbilityEffect* CostEffect = DuplicateObject(AbilityCost->GetDefaultObject<UGMCAbilityEffect>(), this);
	FGMCAbilityEffectData EffectData = CostEffect->EffectData;
	EffectData.OwnerAbilityComponent = OwnerAbilityComponent;
	EffectData.SourceAbilityComponent = OwnerAbilityComponent;
	AbilityCostInstance = OwnerAbilityComponent->ApplyAbilityEffect(CostEffect, EffectData);

	// A cost that outlives this call (Ticking / Persistent / Periodic) ends with the ability on
	// every end path, like any effect applied with HandlingAbility.
	if (AbilityCostInstance && !AbilityCostInstance->bCompleted)
	{
		DeclareEffect(AbilityCostInstance->EffectData.EffectID, EGMCAbilityEffectQueueType::Predicted);
	}
}

void UGMCAbility::RemoveAbilityCost() {
	// An already-ended cost (Instant, or declared and ended with the ability) is a silent no-op in
	// RemoveActiveAbilityEffect.
	if (AbilityCostInstance && OwnerAbilityComponent) {
		const int CostID = AbilityCostInstance->EffectData.EffectID;
		OwnerAbilityComponent->RemoveActiveAbilityEffect(AbilityCostInstance);

		// Forget the declaration: a removed cost is purged from ActiveEffects on the next effect tick,
		// after which its id no longer resolves and FinishEndAbility would take the [EffectLeak] tag
		// fallback, removing the first live effect with the cost's tag (another instance's cost, or
		// any effect sharing the tag).
		DeclaredEffect.Remove(CostID);
		DeclaredEffectTags.Remove(CostID);
		AbilityCostInstance = nullptr;
	}
}

TMap<FGameplayTag, float> UGMCAbility::GetAbilityCostValues() const
{
	// Per attribute, the change the cost would make now: projected minus current.
	TMap<FGameplayTag, float> CostMap;
	if (!AbilityCost) return CostMap;

	if (!OwnerAbilityComponent)
	{
		// No owner (a class default object, typically read by hotbar or tooltip UI): the raw modifier
		// values summed per attribute, Conditions unresolved. An attribute-, custom- or externally-sourced
		// value has nothing to resolve against here and contributes 0 (GetValue would log an Error for
		// it on every poll).
		for (const FGMCAttributeModifier& Modifier : AbilityCost->GetDefaultObject<UGMCAbilityEffect>()->EffectData.Modifiers)
		{
			float& Sum = CostMap.FindOrAdd(Modifier.AttributeTag);
			if (Modifier.ValueType == EGMCAttributeModifierType::AMT_Value)
			{
				Sum += Modifier.GetValue();
			}
		}
		return CostMap;
	}

	for (const TPair<FGameplayTag, float>& Projected : ProjectAbilityCost(1.f))
	{
		const FAttribute* Attribute = OwnerAbilityComponent->GetAttributeByTag(Projected.Key);
		CostMap.Add(Projected.Key, Projected.Value - (Attribute ? Attribute->Value : 0.f));
	}
	return CostMap;
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
	const FGMCAbilityTaskData* Ptr = TaskData.IsValid() && TaskData.GetScriptStruct()->IsChildOf(FGMCAbilityTaskData::StaticStruct())
		? TaskData.GetPtr<FGMCAbilityTaskData>() : nullptr;
	if (!Ptr)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("[TaskDiag] HandleTaskData: payload (struct %s) for TaskID=%d is not a FGMCAbilityTaskData; dropped (ability %s, owner %s)."),
			TaskData.GetScriptStruct() ? *TaskData.GetScriptStruct()->GetName() : TEXT("none"), TaskID,
			*AbilityTag.ToString(), *GetNameSafe(GetOwnerActor()));
		return;
	}
	const FGMCAbilityTaskData TaskDataFromInstance = *Ptr;
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
			UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Ability (tag) %s lists its own tag in CancelAbilitiesWithTag; an ability cannot cancel itself on activation, the entry is ignored."), *AbilityTag.ToString());
			continue;
		}

		if (OwnerAbilityComponent->CancelAbilitiesByTag(AbilityToCancelTag)) {
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
				ActiveAbility.Value->SetPendingCancel();
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

void UGMCAbility::SetPendingCancel() {
	bCancelPending = true;
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
	// GMAS has no separate avatar: the owner actor is the avatar.
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

	// Defensive: both callers (EndAbility, CancelAbility) are latched by bEndRequested, so a declared
	// effect's CancelAbilityOnEnd (or any listener) targeting this ability mid-unwind never gets here;
	// kept as the backstop that makes "the unwind runs once" true on its own.
	if (bFinishing) { return; }
	bFinishing = true;

	// Read before the state is written below: a dead-born instance (refused in PreBeginAbility) never
	// began, and its end effects are for an ability that ran, not for a refused press.
	// Same predicate as CancelAbility's capture; both read before any state write, so the cancel hooks
	// and this gate always agree.
	const bool bHadBegun = AbilityState != EAbilityState::PreExecution;

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

	// End handled effect.
	// Predicted's Safe path ensure-rejects outside a GMC tick. Remap to PredictedQueued only when
	// called from an RPC handler (outside any tick); inside a tick, Predicted removes immediately
	// with no delay. The state cannot change inside this call, so it is read once.
	const bool bInsideGMCTick = OwnerAbilityComponent
		&& ((OwnerAbilityComponent->GMCMovementComponent && OwnerAbilityComponent->GMCMovementComponent->IsExecutingMove())
			|| OwnerAbilityComponent->IsInAncillaryTick()
			|| OwnerAbilityComponent->GetNetMode() == NM_Standalone);

	for (const auto& EfData : DeclaredEffect)
	{
		// Skip Auth effect removal on client
		if (EfData.Value == EGMCAbilityEffectQueueType::ServerAuth && !OwnerAbilityComponent->HasAuthority())  { continue;}

		const EGMCAbilityEffectQueueType QueueType =
			(EfData.Value == EGMCAbilityEffectQueueType::Predicted && !bInsideGMCTick)
				? EGMCAbilityEffectQueueType::PredictedQueued
				: EfData.Value;

		if (UGMCAbilityEffect* Effect =	OwnerAbilityComponent->GetEffectById(EfData.Key))
		{
			// Don't try to close effects that are already ended
			if (Effect->CurrentState == EGMASEffectState::Started)
			{
				OwnerAbilityComponent->RemoveActiveAbilityEffectSafe(Effect, QueueType);
			}
			else
			{
				UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Effect Handle %d already ended for ability %s"), EfData.Key, *AbilityTag.ToString());
			}
		}
		else if (const FGameplayTag* DeclaredTag = DeclaredEffectTags.Find(EfData.Key))
		{
			// The id no longer resolves: a replay re-created this effect under a server id the ability
			// never learned. Reaching it by tag is the only way left. Without this the instance is
			// orphaned, and a Persistent effect keeps granting its tags for the rest of the life --
			// the weapon stays in ADS and the reload is refused after a revive.
			// One instance only: another ability may legitimately own a second one.
			const int32 Removed = OwnerAbilityComponent->RemoveEffectByTagSafe(*DeclaredTag, 1, QueueType);
			UE_LOG(LogGMCAbilitySystem, Warning,
				TEXT("[EffectLeak] Declared effect id %d no longer resolves for ability %s. Removed %d instance(s) by tag %s."),
				EfData.Key, *AbilityTag.ToString(), Removed, *DeclaredTag->ToString());
		}
	}

	// Chain hooks: apply / remove effects when this ability ends. Reuses bInsideGMCTick from the
	// DeclaredEffect removal block above — Predicted requires being inside a GMC tick or
	// Standalone, otherwise PredictedQueued is used to defer until the next safe window.
	// Skipped for a dead-born instance (bHadBegun above).
	if (bHadBegun && OwnerAbilityComponent && (ApplyEffectOnEnd.Num() > 0 || !RemoveEffectOnEnd.IsEmpty()))
	{
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
	return OwnerAbilityComponent && OwnerAbilityComponent->GetCooldownForAbility(AbilityTag) > 0;
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

	// Cache the tag now, while the id still resolves. FinishEndAbility needs it to reach the effect
	// after a replay renumbered it. A live effect only: a completed one (Instant, or already removed
	// by a listener) has nothing left to end, and a cached tag for it would let the [EffectLeak] tag
	// fallback at ability end remove an unrelated live effect sharing the tag.
	if (OwnerAbilityComponent)
	{
		if (const UGMCAbilityEffect* Effect = OwnerAbilityComponent->GetEffectById(OutEffectHandle))
		{
			if (!Effect->bCompleted && Effect->EffectData.EffectTag.IsValid())
			{
				DeclaredEffectTags.Add(OutEffectHandle, Effect->EffectData.EffectTag);
			}
		}
	}
}

bool UGMCAbility::PreBeginAbility()
{
	if (IsOnCooldown())
	{
		UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability Activation for %s Stopped By Cooldown"), *AbilityTag.ToString());
		CancelAbility();
		return false;
	}

	// An AbilityCost the owner cannot pay refuses the activation here, before any event of the ability
	// runs; CommitAbilityCost is still the ability's own call.
	if (!CanAffordAbilityCost())
	{
		UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability Activation for %s Stopped By Cost"), *AbilityTag.ToString());
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

	// Begun exactly when listeners are told it activated: a listener's CancelAbility() on the ability
	// it was handed is the cancel of a begun ability (cancel hooks fire) and refuses the activation
	// here, before the cooldown and BeginAbilityEvent. BeginAbility and every override of it are thus
	// entered only by an ability that survived its activation broadcast; an override that starts
	// tasks or declares effects after Super never does so on an Ended instance.
	AbilityState = EAbilityState::Initialized;
	OwnerAbilityComponent->OnAbilityActivated.Broadcast(this, AbilityTag);
	if (AbilityState == EAbilityState::Ended)
	{
		UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability Activation for %s Stopped by an OnAbilityActivated listener"), *AbilityTag.ToString());
		return false;
	}

	bPassedActivationGates = true;
	BeginAbility();

	return true;
}


void UGMCAbility::BeginAbility()
{
	// End-other-on-begin query (misnamed BlockOtherAbilitiesQuery): cancel matching abilities.
	if (!BlockOtherAbilitiesQuery.IsEmpty())
	{
		FGameplayTagQuery EndQuery = BlockOtherAbilitiesQuery;
		for (auto& ActiveAbility : OwnerAbilityComponent->GetActiveAbilities())
		{
			const FGameplayTagContainer& ActiveAbilityTags = ActiveAbility.Value->AbilityDefinition;

			if (EndQuery.Matches(ActiveAbilityTags))
			{
				ActiveAbility.Value->SetPendingCancel();
				UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability %s cancelled ability %s (matching query)"),
					*AbilityTag.ToString(), *ActiveAbility.Value->AbilityTag.ToString());
			}
		}
	}

	// Chain: consume the window(s) that admitted this stage. Same queue-type
	// detection as the FinishEndAbility chain hooks.
	if (OwnerAbilityComponent && !ChainConsumeWindowTags.IsEmpty())
	{
		const bool bInsideGMCTick =
			(OwnerAbilityComponent->GMCMovementComponent && OwnerAbilityComponent->GMCMovementComponent->IsExecutingMove())
			|| OwnerAbilityComponent->IsInAncillaryTick()
			|| OwnerAbilityComponent->GetNetMode() == NM_Standalone;
		const EGMCAbilityEffectQueueType ConsumeQueueType =
			bInsideGMCTick ? EGMCAbilityEffectQueueType::Predicted : EGMCAbilityEffectQueueType::PredictedQueued;
		for (const FGameplayTag& WindowTag : ChainConsumeWindowTags)
		{
			if (WindowTag.IsValid())
			{
				OwnerAbilityComponent->RemoveEffectByTagSafe(WindowTag, -1, ConsumeQueueType);
			}
		}
	}

	if (bApplyCooldownAtAbilityBegin)
	{
		CommitAbilityCooldown();
	}

	// Cancel Abilities in CancelAbilitiesWithTag container
	CancelConflictingAbilities();
	// Defensive: this ability is not in ActiveAbilities yet, so a cancel from inside those hooks can only
	// come through a reference a listener stored at the activation broadcast. It lands after the
	// cooldown was committed; the cooldown stays, as for any cancel of a begun ability.
	if (AbilityState == EAbilityState::Ended) return;

	// Execute BP Event
	BeginAbilityEvent();
}

void UGMCAbility::BeginAbilityEvent_Implementation()
{
}

void UGMCAbility::EndAbility()
{
	// bEndRequested: the first end wins. A same-kind re-entry (a declared effect's CancelAbilityOnEnd,
	// a listener on the chain window effect) is the same end; a cross-kind re-entry (an EndAbility()
	// during a cancel's unwind, or a CancelAbility() during a natural end) is ignored on purpose, so
	// exactly one hook set fires. AbilityState only turns Ended once FinishEndAbility returns, so the
	// state alone cannot tell; the latch is set before anything below can reach a listener.
	if (AbilityState == EAbilityState::Ended || bEndRequested) { return; }
	bEndRequested = true;

	// Chain: grant the next stage's window on NATURAL end only —
	// CancelAbility skips this on purpose (interrupted swings don't
	// advance a combo).
	if (OwnerAbilityComponent && ChainWindowTag.IsValid() && ChainWindowDuration > 0.f)
	{
		const bool bInsideGMCTick =
			(OwnerAbilityComponent->GMCMovementComponent && OwnerAbilityComponent->GMCMovementComponent->IsExecutingMove())
			|| OwnerAbilityComponent->IsInAncillaryTick()
			|| OwnerAbilityComponent->GetNetMode() == NM_Standalone;
		const EGMCAbilityEffectQueueType WindowQueueType =
			bInsideGMCTick ? EGMCAbilityEffectQueueType::Predicted : EGMCAbilityEffectQueueType::PredictedQueued;

		FGMCAbilityEffectData WindowData;
		WindowData.EffectTag = ChainWindowTag;
		WindowData.GrantedTags.AddTag(ChainWindowTag);
		// Persistent (not the default Instant — that ends the same frame
		// and the tag never survives) with a finite Duration.
		WindowData.EffectType = EGMASEffectType::Persistent;
		WindowData.Duration = ChainWindowDuration;
		WindowData.bUniqueByEffectTag = true; // single instance per tag: a re-grant during an open window is rejected

		int OutHandle = 0; int OutId = 0; UGMCAbilityEffect* OutEffect = nullptr;
		OwnerAbilityComponent->ApplyAbilityEffect(
			UGMCAbilityEffect::StaticClass(), WindowData, WindowQueueType, OutHandle, OutId, OutEffect);
	}

	FinishEndAbility();
	EndAbilityEvent();
	if (OwnerAbilityComponent) { OwnerAbilityComponent->OnAbilityEnded.Broadcast(this); }
}


void UGMCAbility::CancelAbility() {
	// Same entry latch as EndAbility: the first end wins, one unwind, one hook set.
	if (AbilityState == EAbilityState::Ended || bEndRequested) { return; }
	bEndRequested = true;

	// An activation refused in PreBeginAbility (cooldown, PreExecuteCheck, blocked) is not an
	// interruption: the ability never began, so the cancel hooks stay silent for it.
	const bool bHadBegun = AbilityState != EAbilityState::PreExecution;
	FinishEndAbility();
	if (bHadBegun)
	{
		CancelAbilityEvent();
		if (OwnerAbilityComponent) { OwnerAbilityComponent->OnAbilityCancelled.Broadcast(this); }
	}
}

void UGMCAbility::EndAbilityEvent_Implementation()
{
}

void UGMCAbility::CancelAbilityEvent_Implementation()
{
}

AActor* UGMCAbility::GetOwnerActor() const
{
	return OwnerAbilityComponent ? OwnerAbilityComponent->GetOwner() : nullptr;
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
	// 0 without an owner (a class default object), as the component returns for an unknown tag.
	return OwnerAbilityComponent ? OwnerAbilityComponent->GetAttributeValueByTag(AttributeTag) : 0.f;
}


void UGMCAbility::SetOwnerJustTeleported(bool bValue)
{
	if (OwnerAbilityComponent) { OwnerAbilityComponent->bJustTeleported = bValue; }
}

void UGMCAbility::SetBlockAllOtherAbilities(bool bBlockAll)
{
	bBlockAllOtherAbilities = bBlockAll;
	UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("BlockAllOtherAbilities set to %d on %s"), bBlockAll, *AbilityTag.ToString());
}

void UGMCAbility::ModifyBlockOtherAbilitiesViaDefinitionQuery(const FGameplayTagQuery& NewQuery)
{
	BlockOtherAbilitiesQuery = NewQuery;
	UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("EndOtherAbilitiesOnBegin query modified: %s"), *NewQuery.GetDescription());
}
