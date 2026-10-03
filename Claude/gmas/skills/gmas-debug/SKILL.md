---
name: gmas-debug
description: Use when GMAS or GMC gameplay misbehaves - desync, double activation, late or missing effects, attributes stuck at zero, replay oddities, crashes in the ability component; covers logs, console variables, the debugger and a symptom table.
---

> Behavior below was verified against GMC 2.3.x and GMAS 1.4. Confirm signatures in this project's own headers (paths are relative to each plugin's root, e.g. `Source/GMCCore/Public/Components/GMCReplicationComponent.h`); GMC's source is not part of this plugin.

A GMAS fault is almost always one of three things: the two sides of a move disagreed (a correction and a replay), an operation did not reach the move it was meant for (dropped or forced), or something ran on a replayed or combined move that should have run once. This skill lists the places where GMAS and GMC show that, a procedure that separates the three, how a healthy activation reads in the log, and a symptom table: [references/symptoms.md](references/symptoms.md). The rules the symptoms violate live in `gmas:gmas-rules` and `gmas:gmc-prediction`; the networked test setup in `gmas:gmas-testing`. GMAS paths below are relative to `Source/GMCAbilitySystem/`.

## Surfaces

**Log categories.** Raise one with `Log <Category> Verbose` in the console (`VeryVerbose` for the per-move lines), `-LogCmds="LogGMCAbilitySystem Verbose"` on the command line, or `[Core.Log]` in `DefaultEngine.ini`. All default to `Log`.

| Category | Declared in | Covers |
|---|---|---|
| `LogGMCAbilitySystem` | `Public/GMCAbilitySystem.h` | everything the ability component, abilities, effects, tasks and the bound queue log; the trace tags below |
| `LogGMCReplication` | `Source/GMCCore/Public/Utility/GMCLogCategory.h` | GMC moves, corrections, replays, smoothing, the world-time notice; also GMAS's task heartbeat timeout |
| `LogGMCMovement`, `LogGMCPawn`, `LogGMCController` | same header | the movement components (`VeryVerbose` traces every executed move), pawn lifetime, controllers |
| `LogTemp` | engine | server-side mirrors of `[AbilityCut]`, `[TaskDiag]`, `[BatchOp]` and `[TaskHeartbeat]` (1.4+), written so dedicated-server log exports that allow only a fixed category list still show them |

**Trace tags (1.4+).** Bracketed prefixes in `LogGMCAbilitySystem` lines (`Private/Components/GMCAbilityComponent.cpp`, `Private/Ability/GMCAbility.cpp`, `Private/Ability/Tasks/GMCAbilityTaskBase.cpp`). `op` is the operation id: negative for client-made operations, positive for server-made ones; `move_ts` is the move timestamp, identical on both machines for the same move.

| Tag | Level | Traces |
|---|---|---|
| `[AckTrace:Client:GenTick]`, `[AckTrace:Server:GenTick]` | Verbose | the operation slot read at the start of each prediction tick: on the owner what it is about to process, on the server what the client's move carried |
| `[AckTrace:Client:WroteAck]`, `[AckTrace:Client:BatchEnd]`, `[AckTrace:Client:BatchAckWritten]` | Verbose | the owner acknowledging a server operation (single, or a batch with the acknowledged ids) |
| `[AckTrace:Server:ProcessAck]` | Verbose | the server receiving an acknowledgement: `has_payload`, `has_grace` must both be 1 for it to apply |
| `[ImpulseTrace]` | Verbose | every `AddImpulse` operation applied, on both sides: `auth`, `replay`, `move_ts`, velocity before and after |
| `[ReconnectSnapshot]` | Verbose | an owning client rehydrating active effects after a reconnect |
| `[TaskDiag]` | Verbose for benign end races; Warning for task-id or ability-id divergence; Error for a task registered during a replay | task payload and heartbeat routing (`gmas:gmas-task`) |
| `[AbilityCut]` | Error on the client confirm timeout; Warning for an ability ending with unfinished tasks or force-ended by the server | abnormal ability ends, with a diagnostics dump (below) |
| `[ReplayActivation]` | Warning | an activation whose first run on this side happened inside a replay |
| `[AbilityGate]` | Warning | a single-instance refusal whose blocker is older than 2 s |
| `[EffectLeak]` | Warning | a declared effect id that no longer resolved at ability end (replay renumbering); removed by tag instead |
| `[BatchOp]` | Error | a batched server operation whose payload is missing on this side |
| `[ReplayBurst]` | Warning | the replay-burst detector tripped (settings below) |
| `[ServerProcessOperation]` | Warning | the server rejecting a client-auth ability or effect |
| `[TaskHeartbeat]` | Error (`LogTemp`) | the server watchdog ending an ability after 3 s without heartbeats |
| `[ProcessOp]`, `[ServerOpAccept]`, `[ServerOpDrop]`, `[ApplyTrace]` | Warning, only while `GMAS.LogApplyTrace` is on | every activation and effect operation through `ProcessOperation`, each client operation the server accepted or dropped (with the reason), and a C++ plus script call stack per effect instance created |
| `[BLMoveEnqueue]` | Log | the output of `BL.GMAS.DumpAttrBindMap` |
| `[RPC]` | VeryVerbose | the client receiving the server's confirm, end-ability and end-effect RPCs |

Untagged lines worth knowing, same category: `RPCOnServerOperationAdded: N` (Verbose, the owner received a server operation), `Forcing Operation On Server` (Verbose, the grace expired and the server applied the operation outside the move; one frame after the call for AI and unpossessed pawns), `Activation operation N already consumed on this side (redelivery/replay) — skipped` (Verbose, a replay re-delivering an activation that already ran), `Ability Activation for Ability.Dash Stopped By Cooldown` / `... Failing PreExecution check` / `... Blocked By Other Ability` / `... Stopped By Tags` (Verbose, a refused activation; `Stopped (Already Instanced)` at VeryVerbose), `Effect X Not Confirmed By Server (ID: N), Removing...` (Error), `Dropped client operation N: payload expired after M moves` (Warning), `No Abilities Granted for InputTag` (Error), `Client attempted to apply server-auth event` (Warning), `Client operation type X is not allowed by server as client input` (Error). `[GMAS-LIVE-CHECK] Module loaded` is a one-time version banner at Warning: ignore it.

**Console (1.4+, `Private/Components/GMCAbilityComponent.cpp`, not in Shipping builds).**

| Name | Default | Does |
|---|---|---|
| `GMAS.LogApplyTrace` (bool) | `false` | enables the four gated tags above. The `[ApplyTrace]` stack dump fires for every effect instance created on this machine whose class name contains the filter |
| `GMAS.ApplyTraceFilter` (string) | `Stamina_Recovery` | class-name substring for `[ApplyTrace]` and the effect half of `[ProcessOp]`; the shipped default matches nothing in your project, so set it to your class (`GMAS.ApplyTraceFilter MyEffect_Burn`) or to an empty string for everything before enabling the trace |
| `BL.GMAS.DumpAttrBindMap` (command) | – | logs, for the local player's pawn, each bound attribute's float binding index (`Float[i]` = `Value`, `Float[i+1]` = `RawValue`) with its tag and combine mode. GMC's own sync-data dumps (`gmc.LogClientReplay`, `gmc.LogClientMoveTrace`) list bound values by type and index, so this is how a deviating float becomes an attribute name |

**GMC console variables.** Full table, with the GMC `.cpp` files that define them: [gmc-diagnostics.md](../gmc-prediction/references/gmc-diagnostics.md). The five that matter most for GMAS work:

| Variable | Reach for it when |
|---|---|
| `gmc.ShowNetRole` | anything differs per machine: label every pawn with its role on this machine first |
| `gmc.ShowClientCorrections` | the owner snaps, an ability or effect misbehaves after a correction: marks each correction (predicted red, server green) |
| `gmc.LogClientReplay` | a correction needs a name: dumps the client state, the server state and the replayed history, every bound value by type and index |
| `gmc.StatNetMovementValues` | the live movement values of pawns with a chosen role (1 authority, 2 autonomous, 3 simulated) on screen |
| `gmc.LogClientMoveTrace` | moves seem not to arrive: follows individual moves around the round trip |

**Gameplay Debugger.** The runtime module registers category `GMCAbilitySystem` (`Private/GMCAbilitySystem.cpp`, slot 9, enabled in game and simulate). Open the debugger with its activation key (`'` by default, an engine setting) and enable the category. For the debugger's selected actor it prints the server's data pack (`[server]`) and the local component (`[client]`) for granted abilities, active abilities, bound active tags, client-auth tags (1.4+), attributes, active effects, active effects data and cached operation payloads (1.4+); a row whose count differs between the two is marked `[INCOHERENCY]` in red. On a listen server the server rows are read from the movement component's server state, so the host's own pawn shows its authoritative view.

**Project Settings → GMC Ability System (1.4+)**, stored in `DefaultGame.ini`:

| Section / class | Field | Default | Meaning |
|---|---|---|---|
| Network Timing, `UGMASNetworkTimingSettings` (`Public/Settings/GMASNetworkTimingSettings.h`) | `ClientEffectApplicationTimeout` | 0.5 s (min 0.05) | how long a client holds a `Predicted` effect waiting for the server's bound active-effect ids before it becomes `Timeout` and is removed with `Not Confirmed By Server` |
| | `DefaultClientGraceTime` | 0.5 s (min 0.05) | the intended bilateral defer window for removing ticking and periodic effects (`ClientGraceTime` = 0 on the effect means this). On the 1.4 tree `RemoveActiveAbilityEffect` (`Private/Components/GMCAbilityComponent.cpp`) disables the defer, so the value changes nothing |
| Replay Burst Diagnostics, `UGMASReplayBurstSettings` (`Public/Diagnostics/GMASReplayBurstSettings.h`) | `bEnableDetection` | `true` | master switch; off also clears the buffer |
| | `BurstThreshold` | 5 (min 2) | occurrences inside the window that trip the alert |
| | `WindowSeconds` | 2.0 (min 0.1) | sliding window, wall-clock |
| | `WarningWidgetClass`, `WidgetZOrder`, `WidgetDurationSeconds` | none, 100, 3 s | optional UMG widget added to the local player's screen on each alert; one at a time; `<= 0` keeps it until the next alert |

The detector counts *frames in which the owner's prediction tick ran while `CL_IsReplaying()` was true*, one occurrence per frame however many moves were replayed, on the local autonomous proxy only (`GenAncillaryTick`). Crossing the threshold logs `[ReplayBurst] <Pawn> observed N replay occurrences within W s (threshold=T)`, broadcasts `OnReplayBurstDetected(BurstCount, WindowSeconds)` (`BlueprintAssignable`) and resets the buffer, so a sustained chain refires about once per window. It tells you the client is being corrected repeatedly; it does not say by which value (use `gmc.LogClientReplay` and `BL.GMAS.DumpAttrBindMap` for that), and an honest lag spike trips it too.

## Procedure

1. **Reproduce under a network.** Dedicated server plus at least one client in PIE, with network emulation (latency and some loss), the way `gmas:gmas-testing` sets it up. Prediction, replays, combined moves, grace forcing and smoothing do not exist in a standalone game, and a listen host's own pawn is never predicted or corrected.
2. **Raise the logs on both machines.** `Log LogGMCAbilitySystem Verbose` and `Log LogGMCReplication Verbose` on the server and on the client; `gmc.ShowNetRole 1` and `gmc.ShowClientCorrections 1` on the client. For one effect or one activation, set `GMAS.ApplyTraceFilter` and `GMAS.LogApplyTrace 1` on both (1.4+). Keep the Gameplay Debugger open on the pawn.
3. **Align the two logs.** Match lines by operation id (`op=N`), ability id (`AbilityID=`; on 1.4 it is `op × 16`, then the candidate index further from zero, so the two sides print the same number), effect id, and `move_ts` / `ActionTimer`, which is the move timestamp and therefore the same value on both machines for the same move. Wall-clock alignment: `gmc.LogNetWorldTime`. If timestamps live in different domains, there is no `AGMC_WorldTimeReplicator` (`gmas:gmc-prediction`): fix that before anything else. The side that logged the first abnormal line is the root cause; the other side follows seconds later.
4. **Reduce.** Does it reproduce standalone? Then it is logic, not prediction: debug it there. Only with a client? Then one of: a replay (`gmc.LogClientReplay` block right before), a dropped or forced operation (`[ServerOpDrop]`, `Forcing Operation On Server`), or a one-shot on a combined or replayed move. Only on simulated proxies? Smoothing and `SimulationMode`. Remove features one at a time (effects, then tasks, then the ability) until the trace is clean, then add the last one back.
5. **Run the specs for the area** before touching code: `Automation RunTests GMAS.Unit.Effect` (or `GMAS.Unit.Ability`, `GMAS.Unit.Duration`, `GMAS.Unit.Attribute`) headless from a project with GMC and GMAS enabled, plus your own. A green spec with a red PIE session narrows the fault to the network path.
6. **One hypothesis, one change.** Name the bound value, operation or tick you believe is wrong, change that alone, replay the same reproduction and compare the trace line by line. Turn the diagnostics off again (`GMAS.LogApplyTrace 0`, verbosity back to `Log`) before committing.

## Reading the traces

**A healthy predicted activation** (owning client presses `Input.Dash`, 1.4+ line shapes):

1. Client: `QueueAbility` makes operation `-k` and queues it. In the next move's prediction tick: `[AckTrace:Client:GenTick] OperationData op=-k struct=...AbilityActivationOperation move_ts=T auth=0`; at VeryVerbose `[Server: 0] Generated Ability Activation ID: id` with `id = -k × 16 - index`; the ability begins. Nothing more logs on success.
2. Server, when that move executes: `[AckTrace:Server:GenTick] OpData received from client output: op=-k struct=... move_ts=T` (the same `T`); with `GMAS.LogApplyTrace`: `[ServerOpAccept] op=-k ... fromMove=1` then `[ProcessOp] op=-k activate_ability tag=Input.Dash auth=1 fromMove=1 force=0`; `[Server: 1] Generated Ability Activation ID: id` with the same `id`; the server runs the same gates and begins the ability.
3. Server → client: `RPCConfirmAbilityActivation(id)` sets the client instance's confirmed flag; the client logs it only at VeryVerbose (`[RPC] Server Confirmed Long-Running Ability Activation: id`). At Verbose the proof of health is the *absence* of `[AbilityCut] Client removing unconfirmed ability` after `ServerConfirmTimeout` (2 s), and effects the ability applied moving from `Pending` to `Validated` in the debugger without a `Not Confirmed By Server` line. On the ancillary tick the owner sends one task heartbeat per second while tasks run (`gmas:gmas-task`).

**A healthy server operation** (`ServerAuth` effect, `AddImpulse`, activation on a server-controlled pawn): server `EnqueueServerOperation` queues `op=k` with a 1 s grace and sends it; client `RPCOnServerOperationAdded: k` (Verbose); the client's next move carries it: `[AckTrace:Client:GenTick] op=k`, the operation is applied there (`[ImpulseTrace] op=k auth=0 replay=0 move_ts=T ...`) and an acknowledgement is written (`[AckTrace:Client:WroteAck] op=k`, or `BatchEnd` / `BatchAckWritten` for several); server, when that move executes: `[AckTrace:Server:ProcessAck] op=k has_payload=1 has_grace=1 fromMove=1`, applies it in the same move (`[ImpulseTrace] op=k auth=1 ... move_ts=T`) and clears the grace.

**The three deviations:**

| Looks like | Lines | Meaning |
|---|---|---|
| **Dropped** | server `[ServerOpDrop] op=-k reason=IsValidClientOperation_failed` (with `Client operation type X is not allowed by server as client input`, Error) or `reason=IsValidGMASOperation_failed`; or client `Dropped client operation -k: payload expired after M moves`; then client `[AbilityCut] Client removing unconfirmed ability after 2.00s` (Error) and, if the ability had a task, server `[TaskDiag] Heartbeat for unknown AbilityID=id` (Warning) | the client's operation never entered the server pipeline. The client predicted an activation the server never ran |
| **Forced** | server `Forcing Operation On Server` (Verbose) about 1 s after it sent `op=k`, `[ProcessOp] op=k ... force=1`, `[ImpulseTrace] op=k auth=1 ... from_movement_tick=0`; no `[AckTrace:Server:ProcessAck] op=k ... has_grace=1` ever | the owning client never acknowledged: it could not apply the operation (candidates failed their gates there, a batch payload missing: `[BatchOp]`) or its moves stopped. The server applied it outside any move; the two sides disagree until the next correction. For AI and unpossessed pawns a force one frame after the call is the normal path (1.4+) |
| **Replayed** | client `Client replay :::` block (`gmc.LogClientReplay`), `[AckTrace:Client:GenTick]` repeating earlier `move_ts` values, `Activation operation -k already consumed on this side (redelivery/replay) — skipped` (Verbose), `[ImpulseTrace] ... replay=1` | a correction re-ran the pending moves. Those three lines are healthy. `[ReplayActivation] op=-k ... first delivery` (Warning) is not: the original run was discarded or refused here, so the server may hold an instance this client never had. `[TaskDiag] Progress payload RE-dispatched during replay` (Warning) names a task still registered that saw its payload twice |

**`[AbilityCut]`** marks the three ways an ability dies abnormally: the client confirm timeout (Error, `Client removing unconfirmed ability after N s (no RPCConfirmAbilityActivation received)`), `FinishEndAbility` with tasks still running (Warning, both sides, `Ability ending with N unfinished task(s)`: the side that logs it first is the cause), and `RPCClientEndAbility` reaching an instance still active on the client (Warning, `Server force-ended ability that was still active locally`). Each line ends with the ability's diagnostics: `Ability= Tag= AbilityID= State= ServerConfirmed= MovementTick= Authority= Replaying= ActionTimer= ClientStartTime= Age= Tasks=N`, then one line per task with its class, state, `Completed`, heartbeats received and the age of the last heartbeat received and sent. Note that the confirm-timeout path ends the ability through `EndAbility()`, a natural end: its end event runs and a chain window opens for an activation the server never ran.

## Symptoms

The full table, symptom → likely cause → how to confirm → fix and skill: [references/symptoms.md](references/symptoms.md). The five most common:

- **An attribute reads 0 or will not change.** The default clamp holds it at 0 (1.4+), the data asset was added after the bind, or the change went through the inert `SetAttributeValueByTag`. Confirm in the debugger's *Attributes* row; fix the clamp or the bind order (`gmas:gmas-attribute`, `gmas:gmas-setup`).
- **An ability fires twice, or its FX play twice.** One-shot work on a replayed or combined move. Confirm with `gmc.ShowClientCorrections` and a doubled `[ApplyTrace]`; guard with the live-move condition and `CL_DoNotCombineNextMove()` (`gmas:gmas-ability`).
- **An activation lands about a second late.** A server operation the owner could not apply, forced after the grace. Confirm with `RPCOnServerOperationAdded` on the client and `Forcing Operation On Server` on the server; design the first candidate to pass on both sides (`gmas:gmas-rules`).
- **An effect is missing on one client.** Wrong queue type for the caller, or a forced operation. Confirm with the debugger's `[INCOHERENCY]` marker on *Active Effects*; pick the queue type from `gmas:gmas-effect`.
- **An ability ends by itself after about 2 s.** The server never confirmed it: dropped, refused by a gate the client passed, or (pre-1.4) ids that disagree. Confirm with `[AbilityCut] Client removing unconfirmed ability` and the server's `[ServerOpDrop]` or `Stopped By` line; make both sides run the same gates (`gmas:gmas-ability`).

## Checklist

- Reproduced under networked PIE with a dedicated server, a client and network emulation, not standalone; roles checked with `gmc.ShowNetRole`.
- `LogGMCAbilitySystem` and `LogGMCReplication` at Verbose on both machines; the two logs aligned by `op`, `AbilityID`, effect id and `move_ts`; the first abnormal line identified and its machine named as the cause.
- The deviation classified: dropped (`[ServerOpDrop]`, `payload expired`, `[AbilityCut]`), forced (`Forcing Operation On Server` without a matching `ProcessAck`), or replayed (`Client replay :::`, `[ReplayActivation]`), or none of the three (standalone bug).
- For an attribute fault: debugger *Attributes* row and `BL.GMAS.DumpAttrBindMap` consulted; the clamp and the bind order verified.
- For repeated corrections: `[ReplayBurst]` taken as a signal only; the deviating value named with `gmc.ForceFullSyncDataValidation` plus `gmc.LogClientReplay`, mapped to an attribute with `BL.GMAS.DumpAttrBindMap`.
- The matching row of [references/symptoms.md](references/symptoms.md) read and its skill followed; `GMAS.*` specs for the area green before and after the change.
- One change per reproduction; after the fix the session shows no `[AbilityCut]`, no Warning-level `[TaskDiag]`, no `[BatchOp]`, no `Not Confirmed By Server`, no corrections at the moment of the former fault.
- Diagnostics reverted before committing: `GMAS.LogApplyTrace 0`, verbosity back to `Log`, no trace settings left in `DefaultEngine.ini` or `DefaultGame.ini`.
