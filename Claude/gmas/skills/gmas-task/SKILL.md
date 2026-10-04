---
name: gmas-task
description: Use when creating or modifying a GMAS ability task (UGMCAbilityTaskBase subclass) - target data, waits, heartbeat, tick placement, replay safety.
---

> Behavior below was verified against GMC 2.3.x and GMAS 1.4. Confirm signatures in this project's own headers (paths are relative to each plugin's root, e.g. `Source/GMCCore/Public/Components/GMCReplicationComponent.h`); GMC's source is not part of this plugin.

A GMAS task is how an ability waits for something, or carries a value the owning client knows into the move so the server sees the same value in the same move. Tasks live in `Public/Ability/Tasks/` (paths below are relative to `Source/GMCAbilitySystem/`). How an ability is built and where its own hooks run: `gmas:gmas-ability`; the rules on bound state and replay that every task inherits: `gmas:gmas-rules`, `gmas:gmc-prediction`.

## What a task is

`UGMCAbilityTaskBase` (`Public/Ability/Tasks/GMCAbilityTaskBase.h`) is a `UGameplayTask` whose owner (`IGameplayTaskOwnerInterface`) is the `UGMCAbility` and whose tasks component is the ability's `UGMC_AbilitySystemComponent` (the ASC derives from `UGameplayTasksComponent`). Create it with the class's static factory (`NewAbilityTask<T>(OwningAbility)` inside), bind the delegate pins, then call `ReadyForActivation()`; Blueprint async nodes do the last step for you. `OnGameplayTaskInitialized` on the ability fills `Ability` and `AbilitySystemComponent` before `Activate` runs.

| Member | What it is |
|---|---|
| `TaskID` (`int`) | per-ability counter from `UGMCAbility::GetNextTaskID()` (first task = 0), assigned in `Activate` → `RegisterTask`, stored in the ability's `RunningTasks`. The only link between the client twin and the server twin: both sides must create the same tasks in the same order |
| `Ability`, `AbilitySystemComponent` (`TWeakObjectPtr`) | the owning ability and its ASC; `AbilitySystemComponent->ActionTimer` (`double`) is the move clock, `->GMCMovementComponent` the pawn's movement component |
| `Activate()` | registers the task and arms the heartbeat. Call `Super` first |
| `Tick(float)` | called from the ability's `Tick` (`TickTasks`), prediction tick, before `TickEvent`; runs on live, combined and replayed moves |
| `AncillaryTick(float)` | called from the ability's `AncillaryTick` (`AncillaryTickTasks`), before `AncillaryTickEvent`; never replayed. The base implementation is the heartbeat: call `Super` |
| `ClientProgressTask()` | the owning side packs an `FGMCAbilityTaskData` (`TaskType = Progress`, `AbilityID`, `TaskID`) into an `FInstancedStruct` and calls `QueueTaskData` on the ASC |
| `ProgressTask(FInstancedStruct&)` | both sides receive the payload here, inside the move that carried it. Empty in the base |
| `Heartbeat()` | server side: stamps the last heartbeat time (real time, 1.4+) |
| `EndTaskGMAS()` | what the ability calls on every registered task when it ends (`FinishEndAbility`); `EndTask()` by default |
| `OnDestroy(bool)` | every end path funnels here once; the base removes the task from `RunningTasks` (1.4+). Do your cleanup, then `Super` |
| `bTaskCompleted` / `IsTaskCompleted()` | the completion latch; the dispatcher skips `Progress` payloads for a task that set it (1.4+) |
| `DrivesPawnLocally()` (1.4.1+) | true on the machine that produces the pawn's moves: any client (in practice the autonomous proxy), standalone, and a server for a pawn it controls locally (listen host, AI). False on a server for a remotely controlled pawn, which only waits for that client's payloads. Replaces `IsClientOrRemoteListenServerPawn()` (same answer, inverted name; deprecated, removed in 1.5) |

**Client and server halves.** Each side runs the ability's code and so creates its own instance of every task. Nothing about the task object is replicated; what crosses is the payload. `QueueTaskData(const FInstancedStruct&)` appends to `QueuedTaskData`; the ASC's `PreLocalMoveExecution` moves the oldest one (FIFO, 1.4+; LIFO before) into the bound `TaskData` slot (`ClientAuth_Input`, `CombineIfUnchanged`), so it rides the next move's input and reaches the server with it. On both machines `SendTaskDataToActiveAbility` hands the slot to `UGMCAbility::HandleTaskData` → `ProgressTask` on the task with that `TaskID`: in the prediction tick for abilities with `bActivateOnMovementTick = true` (replayed with the move), in the ancillary tick for the others; the slot is cleared at the end of the ancillary tick. One payload per move: `N` queued payloads take `N` moves. A payload for a `TaskID` this side never issued is dropped with a `[TaskDiag] ... never issued on this side` warning (1.4+): the sign that the two sides created different tasks. 1.4.1+: a payload whose struct is not an `FGMCAbilityTaskData` is dropped with one Error at dispatch, before any task sees it.

**Heartbeat watchdog.** From `AncillaryTick`, every live task on a client sends `RPCTaskHeartbeat(AbilityID, TaskID)` (Server, Reliable) once per `HeartbeatInterval` = 1 s of real time (`FPlatformTime::Seconds()`, 1.4+; 0.2 s of `ActionTimer` before). On a dedicated or listen server, for a remotely controlled pawn, the twin checks the age of its last stamp: more than `HeartbeatMaxInterval` = 3 s and it logs `[TaskHeartbeat] Timeout: cancelling ability ...` (Error, `LogGMCAbilitySystem`, 1.4.1+), broadcasts the ASC's `OnTaskTimeout(AbilityTag)`, calls `Ability->CancelAbility()` (no end event, no chain window; `CancelAbilityEvent` runs) and ends itself; `RPCClientEndAbility` then cancels the client instance. On 1.4.0 it logged `Server Task Heartbeat Timeout` on `LogGMCReplication` plus the dump on `LogTemp` and called `EndAbility()`, the natural end. Activation seeds the stamp one interval ahead, so the first kill comes no sooner than about 6 s after the task started. Finished tasks neither send nor check (1.4+); locally controlled server pawns do neither. A task that exists on the server only is killed this way after 6 s; one that exists on the client only beats a `TaskID` the server never issued (`[TaskDiag] Heartbeat for TaskID=... never issued`, 1.4+). The watchdog covers only abilities with a live task: an ability whose tasks all ended but that never calls `EndAbility` runs unwatched, and the server logs `[TaskDiag] Ability task-less for N s and still active` at Verbose after 6 s (1.4+).

**Ending.** `EndTask()` (engine) moves the task to `Finished` and calls `OnDestroy(false)`; `TickTasks` skips finished tasks (1.4+; before, every entry stayed registered and ticked). When the ability ends it calls `EndTaskGMAS()` on everything still in `RunningTasks`, after logging `[AbilityCut] Ability ending with N unfinished task(s)` (Warning, 1.4+) if any task had not completed: an ability that ends normally has no running tasks left.

## Built-in tasks as models

Every factory takes `UGMCAbility* OwningAbility` first (omitted below); Blueprint shows the display names ("Wait For Input Key Press"), C++ uses the factory names in the second column.

| Class (`UGMCAbilityTask_*`) | Factory | Waits for or sends | Ticks in | Completes |
|---|---|---|---|---|
| `WaitDelay` | `WaitDelay(Time)` | `Time` seconds of move time | prediction | `TimeStarted + Time <= ActionTimer` in `Tick`; `Completed`. No payload: both sides compute the same answer |
| `WaitForInputKeyPress`, `WaitForInputKeyRelease` | `WaitForKeyPress(bCheckForPressDuringActivation = true, MaxDuration = 0)`, `WaitForKeyRelease(bCheckForReleaseDuringActivation = true, MaxDuration = 0)` | the Enhanced Input `Started` / `Completed` event of the ability's `AbilityInputAction` on the owning machine | ancillary (`OnTick(Duration)` each tick) | the event calls `ClientProgressTask` on the driving machine (`DrivesPawnLocally()`, 1.4.1+; a server for a remote pawn binds and queues nothing); both sides fire `Completed(Duration)` or, past `MaxDuration`, `TimedOut(Duration)` when the payload's move is processed. With `bCheck...DuringActivation`, `Press` completes when the key is already **pressed** (1.4.1+; 1.4.0's poll was inverted) and `Release` when it is already released; neither polls during a replay (1.4.1+). Without an input action or an Enhanced Input component they complete through the payload at once; `Press` also when `bAllowMultipleInstances` is set (Warning) |
| `WaitForInputKeyPressParameterized` | `WaitForKeyPress(InputAction, bCheckForPressDuringActivation = true, MaxDuration = 0)` | same, for any `UInputAction` | ancillary | same pins; binds, polls and gates on its own `InputAction` (1.4.1+; 1.4.0 read the ability's `AbilityInputAction`) |
| `SetTargetData<Byte, Int, Float, Vector3, Transform, Hit, GameplayTag, Object, InstancedStruct>` | `SetTargetData<Type>(Value)`, e.g. `SetTargetDataVector3(Vector3)`, `SetTargetDataHit(InHit)` | sends one value from the owning machine | – | `Activate` calls `ClientProgressTask` where `DrivesPawnLocally()`; `ProgressTask` fires `Completed(Value)` on both sides in the move carrying it, then `EndTask`. Every variant compares the payload's struct with its own exactly and drops a mismatch with one Error (1.4.1+; only `Vector3` checked on 1.4.0) |
| `WaitForGameplayTagChange` | `WaitForGameplayTagChange(WatchedTags, ChangeType = Changed)` | a matching tag added (`Set`), removed (`Unset`) or either | – | `AddFilteredTagChangeDelegate` callback from `CheckActiveTagsChanged`, which runs in the ancillary tick and in `GenSimulationTick` (proxies: no task lives there), each machine on its own; `Completed(MatchedTags)` once, after `EndTask`; the handle is removed in `OnDestroy` (1.4.1+; 1.4.0 leaked the binding and could fire twice). A prediction the server corrects leaves the two sides disagreeing |
| `WaitForGMCMontageChange` | `WaitForGMCMontageChange()` | `GetActiveMontage(MontageTracker)` on the organic movement component to differ from the one at activation | prediction | `Completed(CurrentMontage)`; ends at once with an Error log without an organic component or a running montage |
| `RotateYawTowardsDirection` | `WaitForRotateYawTowardsDirection(TargetDirection, RotationSpeed)` | the pawn's yaw to reach the direction | prediction | drives `RotateYawTowardsDirection(Direction, Rate, DeltaTime)` (`Source/GMCCore/Public/Components/GMCMovementUtilityComponent.h`) each tick; `Completed(Duration)` within 0.01°. `Duration` is move time (`ActionTimer`, 1.4.1+; world time on 1.4.0, cosmetic only) |

Patterns the built-ins show:

- **Local wait** (`WaitDelay`, montage, yaw): read bound state and `ActionTimer` in `Tick`, complete on both sides independently. No payload, no network cost, replay-exact.
- **Owner-known value** (`SetTargetData*`, input tasks): only the owning machine has the fact; `ClientProgressTask` sends it, `ProgressTask` completes both twins in the same move. The input tasks are the model for a wait that ends on such a fact; `SetTargetData*` for a value computed at activation.
- **Deterministic timeout** (input tasks, 1.4+): `MaxDuration` is compared against `ActionTimer - StartTime`; the owner queues the payload when it crosses the limit, and `OnTaskCompleted` re-derives `TimedOut` from the payload move's `ActionTimer` so both sides pick the same pin even when the crossing and the payload land in one server frame.
- **Replay-safe input poll**: the input tasks read the live key state only where `DrivesPawnLocally()` and not while `IsReplayingForGMASLogic()`; the legitimate completion is already in the move history as a payload. 1.4.1+ for all three; on 1.4.0 only `WaitForInputKeyRelease` had the replay guard.

## Writing a wait task

`UMyTask_WaitUntilGrounded` completes when the pawn's movement component reports ground contact, or times out. It completes locally: both sides see the same bound state in the same move.

```cpp
// MyTask_WaitUntilGrounded.h
#pragma once
#include "Ability/Tasks/GMCAbilityTaskBase.h"
#include "MyTask_WaitUntilGrounded.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMyWaitUntilGroundedPin, float, Duration);

UCLASS()
class UMyTask_WaitUntilGrounded : public UGMCAbilityTaskBase
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable) FMyWaitUntilGroundedPin OnCompleted;
    UPROPERTY(BlueprintAssignable) FMyWaitUntilGroundedPin OnTimedOut;

    UFUNCTION(BlueprintCallable, Category = "My|Tasks",
              meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
    static UMyTask_WaitUntilGrounded* WaitUntilGrounded(UGMCAbility* OwningAbility, float MaxDuration = 0.f);

    virtual void Activate() override;
    virtual void Tick(float DeltaTime) override;
protected:
    virtual void OnDestroy(bool bInOwnerFinished) override;
private:
    void Finish(bool bTimedOut);
    float MaxDuration = 0.f;
    double StartTime = 0.0;
};
```

```cpp
// MyTask_WaitUntilGrounded.cpp
#include "MyTask_WaitUntilGrounded.h"
#include "MyMovementCmp.h"                       // the project's movement component, which binds bGrounded
#include "Components/GMCAbilityComponent.h"      // ActionTimer, GMCMovementComponent

UMyTask_WaitUntilGrounded* UMyTask_WaitUntilGrounded::WaitUntilGrounded(UGMCAbility* OwningAbility, float MaxDuration)
{
    UMyTask_WaitUntilGrounded* Task = NewAbilityTask<UMyTask_WaitUntilGrounded>(OwningAbility);
    Task->MaxDuration = MaxDuration;
    return Task;
}

void UMyTask_WaitUntilGrounded::Activate()
{
    Super::Activate();                                      // TaskID, RunningTasks, heartbeat
    StartTime = AbilitySystemComponent->ActionTimer;        // move time, never world or real time
}

void UMyTask_WaitUntilGrounded::Tick(float DeltaTime)       // prediction tick: live, combined and replayed moves
{
    Super::Tick(DeltaTime);
    if (bTaskCompleted || !AbilitySystemComponent.IsValid()) { return; }
    const UMyMovementCmp* Move = Cast<UMyMovementCmp>(AbilitySystemComponent->GMCMovementComponent);
    if (Move && Move->bGrounded) { Finish(false); return; } // bGrounded: a value the movement component binds
    if (MaxDuration > 0.f && AbilitySystemComponent->ActionTimer - StartTime >= MaxDuration) { Finish(true); }
}

void UMyTask_WaitUntilGrounded::Finish(bool bTimedOut)
{
    if (bTaskCompleted) { return; }                         // latch before anything observable
    bTaskCompleted = true;
    const float Duration = static_cast<float>(AbilitySystemComponent->ActionTimer - StartTime);
    EndTask();                                              // Finished -> OnDestroy -> removed from RunningTasks
    if (bTimedOut) { OnTimedOut.Broadcast(Duration); } else { OnCompleted.Broadcast(Duration); }
}

void UMyTask_WaitUntilGrounded::OnDestroy(bool bInOwnerFinished)
{
    // Release what Activate acquired (delegates, input bindings) here, before Super: it runs on
    // every end path exactly once, and Super unregisters the task and marks it garbage.
    Super::OnDestroy(bInOwnerFinished);
}
```

- **Which tick.** A wait on bound state belongs in `Tick`: the prediction tick is where the state is current on both sides, and a replay reproduces the decision. `AncillaryTick` is for work that must not be replayed (the input tasks poll there, because their completion travels as a payload anyway); call `Super::AncillaryTick` when you override it or the heartbeat stops.
- **Timeouts count move time.** `ActionTimer - StartTime` is identical on client, server and replay; `GetWorld()->GetTimeSeconds()` and `FPlatformTime` are not (the latter is the heartbeat's clock only). With a payload-based wait, re-derive `TimedOut` from `ActionTimer` in `ProgressTask` as the input tasks do, so both sides choose the same pin.
- **Local reads only when both sides have them.** The movement component's bound values, attributes and tags (`gmas:gmas-rules`) qualify; input state, local traces against simulated proxies, UI and camera do not: those go through a payload (next section).
- Do not set `bTickingTask`: GMAS ticks tasks itself; the engine flag only schedules `TickTask`, which GMAS tasks do not implement (1.4.1 removed it from the built-ins).

## Writing a target-data task

`UMyTask_SendAimPoint` sends the owner's aim point into the move. The server validates it before anyone acts on it: the payload is client input.

```cpp
// MyTask_SendAimPoint.h
#pragma once
#include "Ability/Tasks/GMCAbilityTaskBase.h"
#include "Ability/Tasks/GMCAbilityTaskData.h"
#include "MyTask_SendAimPoint.generated.h"

USTRUCT()
struct FMyTaskData_AimPoint : public FGMCAbilityTaskData   // the dispatcher reads AbilityID, TaskID, TaskType from the base
{
    GENERATED_BODY()
    UPROPERTY() FVector AimPoint = FVector::ZeroVector;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMySendAimPointPin, FVector, AimPoint);

UCLASS()
class UMyTask_SendAimPoint : public UGMCAbilityTaskBase
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable) FMySendAimPointPin Completed;

    UFUNCTION(BlueprintCallable, Category = "My|Tasks",
              meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
    static UMyTask_SendAimPoint* SendAimPoint(UGMCAbility* OwningAbility, FVector AimPoint);

    virtual void Activate() override;
    virtual void ClientProgressTask() override;                      // owning machine: pack and queue
    virtual void ProgressTask(FInstancedStruct& TaskData) override;  // both machines: validate and complete
private:
    static constexpr float MaxAimDistance = 5000.f;
    FVector AimPoint = FVector::ZeroVector;
};
```

```cpp
// MyTask_SendAimPoint.cpp
#include "MyTask_SendAimPoint.h"
#include "Components/GMCAbilityComponent.h"      // QueueTaskData, GetOwner

UMyTask_SendAimPoint* UMyTask_SendAimPoint::SendAimPoint(UGMCAbility* OwningAbility, FVector InAimPoint)
{
    UMyTask_SendAimPoint* Task = NewAbilityTask<UMyTask_SendAimPoint>(OwningAbility);
    Task->AimPoint = InAimPoint;
    return Task;
}

void UMyTask_SendAimPoint::Activate()
{
    Super::Activate();
    if (DrivesPawnLocally()) { ClientProgressTask(); }   // a server for a remote pawn only waits (1.4.1+ name)
}

void UMyTask_SendAimPoint::ClientProgressTask()
{
    FMyTaskData_AimPoint Data;
    Data.TaskType  = EGMCAbilityTaskDataType::Progress;
    Data.AbilityID = Ability->GetAbilityID();
    Data.TaskID    = TaskID;
    Data.AimPoint  = AimPoint;
    Ability->OwnerAbilityComponent->QueueTaskData(FInstancedStruct::Make(Data));  // rides the next move
}

void UMyTask_SendAimPoint::ProgressTask(FInstancedStruct& TaskData)
{
    Super::ProgressTask(TaskData);
    if (bTaskCompleted) { return; }
    if (TaskData.GetScriptStruct() != FMyTaskData_AimPoint::StaticStruct())
    {
        EndTask();                                          // never Get<>() an unchecked payload: it asserts
        return;
    }
    FVector Point = TaskData.Get<FMyTaskData_AimPoint>().AimPoint;
    const AActor* Owner = Ability->OwnerAbilityComponent->GetOwner();
    const FVector Origin = Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
    Point = Origin + (Point - Origin).GetClampedToMaxSize(MaxAimDistance);  // same clamp on both sides
    bTaskCompleted = true;
    EndTask();
    Completed.Broadcast(Point);                             // same logical move on owner and server
}
```

- **Payload struct** derives from `FGMCAbilityTaskData` (`Public/Ability/Tasks/GMCAbilityTaskData.h`; `TaskType` is always `Progress`, which is `1` on the wire since 1.4.1 removed the unused `Heartbeat` value) and is a `USTRUCT` with `UPROPERTY` fields: the slot is a bound `FInstancedStruct`, serialized by reflection with every move. Keep it small; it is input state on the wire.
- **When `Completed` fires.** Earliest one move after `ReadyForActivation`: the payload enters the slot in the next `PreLocalMoveExecution`, and dispatch runs in that move's prediction tick (movement-tick abilities) or ancillary tick (the others), on the owner when the move executes and on the server when it re-executes the same move. For a listen-host or AI pawn the server is the owning machine and delivers to itself through its own move. The `Completed` handler of a movement-tick ability therefore runs inside replayed code: it may read and write bound state; one-shot work needs the live-move guard from `gmas:gmas-ability`.
- **FIFO (1.4+).** Payloads queued in one frame are delivered in creation order, one per move, so a task chain created in a single graph resolves in order. On older trees the order is reversed: chain tasks from `Completed` handlers instead of creating them together.
- **Validate on both sides.** `ProgressTask` is the server's only chance to reject or clamp what the client sent; apply the same rule on the owner so the two results agree. Check the struct type before `Get<>()`, bound the value, and `EndTask()` on garbage.

## Replay safety

- **Create tasks from once-per-instance code only**: `BeginAbilityEvent`, or another task's `Completed` handler. A task created from `TickEvent` or `AncillaryTickEvent` on a condition is created a different number of times on the two sides (combined moves re-run the tick on the client, replays re-run it, the server runs each move once), which shifts every later `TaskID`: `[TaskDiag] Task ... registered DURING replay` (Error) and `... never issued on this side` warnings (1.4+), payloads delivered to the wrong task, heartbeats that starve the server twin until the watchdog ends the ability. Keep Blueprint branches that create tasks identical on both sides for the same reason.
- **No live polls on a replayed move.** Reading input, the camera or anything not in the move is only valid where `DrivesPawnLocally()` and `!AbilitySystemComponent->IsReplayingForGMASLogic()` (1.4+; `GMCMovementComponent->CL_IsReplaying()` before); the result goes into a payload, never into a local completion. `gmas:gmc-prediction` explains why the replay sees today's key state against yesterday's move.
- **Idempotent completion.** Set `bTaskCompleted` before any broadcast and return early on it in every path; `EndTask()` is idempotent. The dispatcher drops `Progress` payloads for finished tasks (1.4+), but a tick-driven completion has no such guard except yours, and a replayed move re-dispatches its payload to any task still registered (`[TaskDiag] Progress payload RE-dispatched during replay`, Warning).
- **A task decides, the ability acts.** Spawns, FX, RPCs, score and other world side effects do not belong in a task: report through the pin and let the ability's handler apply the live-move guard (`!IsReplayingForGMASLogic() && !IsSimulatedMove()`, `gmas:gmas-ability`). A task's `Tick` is a pure function of bound state, `ActionTimer` and `DeltaTime`.
- **No instance members the replay cannot rebuild.** `StartTime` taken from `ActionTimer` is safe because it is compared against `ActionTimer`; a counter incremented per tick, or a real-time stamp, drifts between the live run and the replay. `gmas:gmas-rules` lists what a replay restores (bound values only: not tasks, not ability instances).

## Checklist

- Factory is `static`, takes `UGMCAbility* OwningAbility` first, uses `NewAbilityTask<T>` and returns the unactivated task; C++ callers bind pins and call `ReadyForActivation()`.
- `Activate` and `AncillaryTick` call `Super`; `OnDestroy` cleans up before `Super`; nothing sets `bTickingTask`.
- Local completion only from bound state and `ActionTimer`; everything the owner alone knows travels as a payload struct derived from `FGMCAbilityTaskData` with `TaskType = Progress`, `AbilityID`, `TaskID` filled.
- `ProgressTask` checks the struct type, clamps or rejects the value with a rule both sides apply, latches `bTaskCompleted`, ends the task, then broadcasts.
- Timeouts compare `ActionTimer - StartTime`; a payload-based wait re-derives `TimedOut` in `ProgressTask`.
- Every completion path is idempotent; no world side effects inside the task; handlers in the ability guard one-shots.
- Tasks are created from `BeginAbilityEvent` or `Completed` handlers, identically on both sides; `AbilityInputAction` is passed to `QueueAbility` when an input task is used.
- Verified under networked PIE with a client (`gmas:gmas-testing`): no `[TaskDiag]` warnings, no `[TaskHeartbeat] Timeout`, no `[AbilityCut] Ability ending with N unfinished task(s)` for a normal completion.

## Which test to write

A spec in the style of `Source/GMCAbilitySystemTests/Private/GMAS_TaskSpec.cpp` (`GMAS.Unit.Task`, 1.4.1+: `WaitDelay`, the tag-watch and bound-attribute test abilities, both confirm-timeout outcomes) and `GMAS_AbilitySpec.cpp` (`Source/GMCAbilitySystem/Private/Tests/` before 1.4.1), on the stub harness `gmas:gmas-testing` describes, plus a test ability that starts your task in `BeginAbilityEvent` and records the pin values (models: `UGMAS_TestDelayAbility`, `UGMAS_TestTagWatchAbility`, `UGMAS_TestBoundAttrAbility`; a `BeginAbility` override returns when the ability ended inside `Super`). Seed the clock with `SetActionTimerForTest` (the bundled specs use `GMASTest::StableActionTimer`, `-1.0`) before activating: `GenerateAbilityID()` is `ActionTimer × 100` and the effect-id generators refuse `ActionTimer == 0`, so an unseeded clock breaks any test ability with a cost. The ownerless stub reports standalone, so `DrivesPawnLocally()` is true and the heartbeat watchdog never fires (its server branch needs a server net mode). Activate with `TryActivateAbility(UMyTestAbility::StaticClass())` and take the instance from `GetActiveAbilities()`. Two drives, because `GenPredictionTick` overwrites `ActionTimer` from the stub's move timestamp (`-1.0` on a component that never executed a move):

- **Target-data task, through moves.** Dispatch does not read `ActionTimer`. Per move: `PreLocalMoveExecution()` (moves the oldest queued payload into the slot), `GenPredictionTick(Dt)` (dispatches it to a movement-tick ability), `GenAncillaryTick(Dt, false)` (dispatches to the others, clears the slot). Assert: `Completed` fired exactly once with the clamped value; `IsTaskCompleted()`; the task is gone from the instance's `RunningTasks`; one more move delivers nothing; a hand-queued payload of another struct type ends the task with no broadcast; two tasks created together complete in creation order (FIFO, 1.4+).
- **Wait task and timeouts, by direct ticks.** Set the clock (`SetActionTimerForTest`, 1.4.1+), then call `TickActiveAbilitiesForTest(Dt)` (every active ability's `Tick`, as the prediction tick does) and `CleanupStaleAbilitiesForTest()`, or the instance's public `Tick(Dt)` / `AncillaryTick(Dt)`. Assert no completion one `Dt` before the condition and completion on it; `OnTimedOut` and not `OnCompleted` at `MaxDuration`. Keep the written clock within `ServerConfirmTimeout` (2 s) of activation, or raise that protected default in the test ability's constructor, or set `bForceAuthorityForTest` (honoured by this check on 1.4.1+, not on 1.4.0): an ownerless component reports no authority, so `UGMCAbility::Tick` treats the instance as an unconfirmed client one and cuts it with `[AbilityCut]` past the timeout.

Heartbeats, `TaskID` symmetry, payload timing under real moves and replay re-dispatch need a networked PIE test with a client (`gmas:gmas-testing`): watch for `[TaskDiag]`, `[TaskHeartbeat]` and `[AbilityCut]` lines on both machines.
