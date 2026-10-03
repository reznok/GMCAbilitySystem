---
name: gmc-prediction
description: Use when writing or reviewing code that runs inside GMC's movement cycle (GenPredictionTick, bound values, replays, combined moves, simulated proxies, move timestamps) or when a GMC pawn behaves differently on client and server.
---

> Behavior below was verified against GMC 2.3.x and GMAS 1.4. Confirm signatures in this project's own headers (paths are relative to each plugin's root, e.g. `Source/GMCCore/Public/Components/GMCReplicationComponent.h`); GMC's source is not part of this plugin.

GMC (General Movement Component) replaces the engine's character movement with a generic, client-predicted, server-authoritative *move* pipeline. Your pawn is an `AGMC_Pawn`, your movement component derives from `UGMC_MovementUtilityCmp` (or `UGMC_OrganicMovementCmp`), player controllers from `AGMC_PlayerController`, AI controllers from `AGMC_AIController`. GMAS's ability component plugs into the same hooks (`gmas:gmas-rules`); everything here applies to GMAS abilities too. Diagnostics: [references/gmc-diagnostics.md](references/gmc-diagnostics.md).

## The move cycle

A *move* is one input state (the bound values flagged as input), one output state (every bound value after execution) and metadata (timestamp, delta time). Only machines that *control* a pawn create moves; everyone else receives states.

- **Owning client (autonomous proxy).** Each frame GMC calls `PreLocalMoveExecution`, saves the input, runs `GenPredictionTick(DeltaTime)` (sub-stepped when the move's delta exceeds the max time step), then `GenAncillaryTick`, then saves the output. The move goes into a history (`MoveHistoryMaxSize`, 256) and completed moves are sent to the server at most `ClientSendRate` (30) times a second.
- **Server.** For a remote pawn it re-executes each received move with the client's input, delta time and timestamp (`SV_IsExecutingRemoteMoves()` is true), compares server-authoritative bound values with what the client reported, and sends back the authoritative state. For pawns it controls itself (listen-server host, AI) it executes one local move per frame, exactly like the client but with nobody to correct.
- **Correction and replay.** When the reply differs from the client's recorded output, the client adopts the server state and re-executes every move still pending after it, in order, with their recorded inputs and delta times (`bUseClientReplay`, default true). `CL_IsReplaying()` is true throughout.
- **Simulated proxies.** Never run `GenPredictionTick`. They buffer the states the server sends and display an interpolated state slightly in the past (`SmoothingMode`, default `AdaptiveBufferedInterpolation`). `GenSimulationTick(DeltaTime)` runs once per frame for cosmetics.

| Hook (`Public/Components/...`) | Runs for | During a replay | Notes |
|---|---|---|---|
| `BindReplicationData_Implementation()` (`GMCReplicationComponent.h`) | every machine, once, before any net serialization | – | the only place to bind |
| `PreLocalMoveExecution_Implementation(const FGMC_Move&)` (`GMCReplicationComponent.h`) | owning client and server-controlled pawns, before the move's input is saved | no | last chance to write input values for this move |
| `GenPredictionTick_Implementation(float DeltaTime)` (`GMCMovementUtilityComponent.h`) | owning client (live, combined and replayed moves), server (local and remote moves), simulated executions | yes | the predicted logic |
| `GenAncillaryTick_Implementation(float DeltaTime, bool bLocalMove, bool bCombinedClientMove)` (`GMCMovementUtilityComponent.h`) | owning client and server, after the move, never sub-stepped | no | `bCombinedClientMove` is true on the client when the move was re-executed rather than newly enqueued; always false on the server |
| `GenSimulationTick_Implementation(float DeltaTime)` (`GMCMovementUtilityComponent.h`) | simulated proxies and smoothed remote pawns on a listen server, every frame | – | cosmetics only; never on a dedicated server |
| `OnSyncDataApplied_Implementation(const FGMC_PawnState&, EGMC_NetContext)` (`GMCReplicationComponent.h`) | every machine, every time GMC loads a state into the pawn | yes, with historical states | see *Detecting state changes* |
| `WorldTickEnd_Implementation(float DeltaTime)` (`GMCReplicationComponent.h`) | every machine, once per frame after all moves, replays and smoothing | no | the place for once-per-frame transitions |

`DeltaTime` passed to the prediction tick is the *move's* delta (derived from timestamps, clamped to `MaxMoveDeltaTime`, 0.05 s), not the frame delta. The predicted logic must be a pure function of the bound input state, the bound previous state and that delta: anything else (wall-clock time, frame delta, unbound members, random numbers without a bound seed, other actors' transforms) diverges between client and server and shows up as corrections.

## Roles

All queries live on `UGMC_ReplicationCmp` (`Public/Components/GMCReplicationComponent.h`).

| Query | True when |
|---|---|
| `IsAutonomousProxy()` | on a client, for the pawn this client controls (`ROLE_AutonomousProxy`) |
| `IsSimulatedProxy()` | on a client, for every pawn it does not control (`ROLE_SimulatedProxy`), including AI pawns |
| `IsLocallyControlledServerPawn()` | on the server, for a pawn controlled there: the listen-server host's own pawn, AI pawns, and every pawn in a standalone game |
| `IsRemotelyControlledServerPawn()` | on the server, for a pawn controlled by a connected client |

Useful refinements: `IsServerPawn()`, `IsClientPawn()`, `IsPredictedAutonomousProxy()`, `IsSmoothedListenServerPawn()`, `SV_IsExecutingRemoteMoves()`, `CL_IsReplaying()`, `IsSimulatedMove()`, `IsLocalMove()`, `IsCombinedMove()` (the last three on `UGMC_MovementUtilityCmp`).

- **Dedicated server:** every player pawn is remotely controlled; AI pawns are locally controlled; nothing is smoothed and `GenSimulationTick` never runs.
- **Listen server:** the host's pawn is a locally controlled server pawn: no prediction, no replays, no corrections, one move per frame. Remote pawns are authoritative *and* smoothed for display on the host (`IsSmoothedListenServerPawn()`), so `GenSimulationTick` runs for them there while gameplay still reads the bound (authoritative) members.
- **Standalone / PIE without a net mode:** the pawn is a locally controlled server pawn. Code that only ever ran standalone has never been replayed, combined or smoothed; test it under a networked PIE (`gmas:gmas-testing`).

## Bound values

GMC replicates nothing you do not bind. State that must carry over between moves, reach the server, be validated, be restored by a replay or be visible on simulated proxies is bound in `BindReplicationData_Implementation` with `Bind<Type>(Variable, PredictionMode, CombineMode, SimulationMode, Interpolation)`. Each call returns a binding index that reads the value back from any saved `FGMC_PawnState`.

Rules: bind only in that function, bind members of the movement component (GMC's own recommendation), call `Super` first so the chain is base → derived → ability component last (`gmas:gmas-rules`), and keep the order identical on every machine: the order *is* the wire layout and the comparison layout. Bindings cannot be removed once made, and bindings that differ between client and server corrupt every state the two exchange.

| Function | Variable type | On the wire |
|---|---|---|
| `BindBool` | `bool` | 1 bit |
| `BindHalfByte` | `uint8` | lower 4 bits |
| `BindByte` | `uint8` | 8 bits |
| `BindInt` | `int32` | full |
| `BindSinglePrecisionFloat` / `BindCompressedSinglePrecisionFloat` | `float` | full / 2 decimals |
| `BindDoublePrecisionFloat` / `BindCompressedDoublePrecisionFloat` / `BindTruncatedDoublePrecisionFloat` | `double` | full / 2 decimals / as float |
| `BindCompressedVector2D`, `BindCompressedVector`, `BindCompressedRotator` | `FVector2D`, `FVector`, `FRotator` | 2 decimals per component |
| `BindActorReference`, `BindActorComponentReference`, `BindAnimMontageReference` | `AActor*`, `UActorComponent*`, `UAnimMontage*` | object reference |
| `BindName`, `BindGameplayTag`, `BindGameplayTagContainer`, `BindInstancedStruct` | `FName`, `FGameplayTag`, `FGameplayTagContainer`, `FInstancedStruct` | serialized |

The four modes (`Public/Replication/SyncSettings.h`, interpolation in `Public/Replication/Smoothing.h`):

- **`EGMC_PredictionMode`:** `ClientAuth_Input` for player input (the server accepts it unquestioned); `ServerAuth_Output_ClientValidated` for state the server owns and the client checks (a deviation triggers a correction); `_Input` / `_Output` / `_InputOutput` say which side of the move the value belongs to; `_ServerValidated` moves the check to the server; `Local` is not synchronized at all.
- **`EGMC_CombineMode`:** `CombineIfUnchanged` (a changed value forces a new move: the right choice for inputs), `AlwaysCombine` (keeps the first value of the window), `AlwaysCombineOverwrite` (keeps the latest), `Default` (GMC's built-in rules for the type). For a one-shot input such as `Input.Dash` held for one frame, `CombineIfUnchanged` guarantees the frame it rises and the frame it falls each start a new move, so the server sees the pulse.
- **`EGMC_SimulationMode`:** `Periodic_*` sends the value with every state update (simulated proxies may miss intermediate values), `PeriodicAndOnChange_*` additionally sends it whenever it changes (for rare flags such as `State.Stunned`), `None` never reaches simulated proxies.
- **`EGMC_InterpolationFunction`:** `Linear`, `Cubic`, `NearestNeighbour`, `StartValue`, `TargetValue`, `Custom1`–`Custom8`; non-numeric values use `NearestNeighbour` or `TargetValue`.

```cpp
void UMyMovementCmp::BindReplicationData_Implementation()
{
    Super::BindReplicationData_Implementation();
    BindBool(bInput_Dash, EGMC_PredictionMode::ClientAuth_Input, EGMC_CombineMode::CombineIfUnchanged,
             EGMC_SimulationMode::None, EGMC_InterpolationFunction::NearestNeighbour);
    BindSinglePrecisionFloat(DashCooldown, EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
             EGMC_CombineMode::Default, EGMC_SimulationMode::Periodic_Output, EGMC_InterpolationFunction::Linear);
    // The ability system component binds after everything else (gmas:gmas-rules).
}
```

Bound values are the *only* state a replay restores. An unbound member that `GenPredictionTick` writes keeps its newest value while older moves are replayed on top of it: the replay produces a different result than the live run did, and the next correction is guaranteed. Either bind it, recompute it from bound values at the start of every tick, or make it a local variable.

## Replays and combined moves

**Replay.** The client sets the pawn to the server's state, then re-executes every pending move. `CL_IsReplaying()` is true for the whole pass (always false on the server and on simulated proxies); `IsLocalMove()` is false; `GenAncillaryTick` is *not* called for replayed moves. A correct replay is invisible. A visible one (snapping, repeated effects, a cooldown that jumps) means a side effect or an unbound dependency leaked into the predicted logic.

**Combined moves.** The client does not enqueue a new move every frame. While the bound inputs allow combining and the accumulated delta stays within `MaxCombinedDeltaTime` (0.03334 s), the newest frame is *merged into the pending move*: the pending move's timestamp advances, its inputs are combined per their combine modes, the pawn is reset to that move's start state and `GenPredictionTick` runs again with the accumulated delta time. At 120 fps a move's tick therefore runs up to four times with a growing `DeltaTime`, and only the last run counts. The server executes each enqueued move exactly once and never sees the intermediate runs (`bCombinedClientMove` is always false there).

Consequence: a one-shot event the tick raises from *bound state* (a cooldown reaching zero, a queued teleport, a shot fired) is raised again by the next combined re-run, because that re-run starts from the state before the event. After such an event call `CL_DoNotCombineNextMove()` (`UGMC_ReplicationCmp`): the current move is sealed and the next frame starts a fresh move from the post-event state. An input bound `CombineIfUnchanged` that flips as the event happens achieves the same for events driven by input.

**Side effects.** Anything the world can observe more than once (spawning actors, FX, sound, RPCs, ability activations, score) runs only on a *live* move:

| Machine | Condition | Why |
|---|---|---|
| owning client | `!CL_IsReplaying() && !IsSimulatedMove()` | replays and simulated executions re-run the same move |
| server | `!IsSimulatedMove()` | remote moves execute once each; local server moves once per frame |
| simulated proxy | never from prediction code | react in `GenSimulationTick` or to bound values that changed |

`IsSimulatedMove()` is true when GMC runs the prediction tick for display or rollback only (the `ForwardSimulation` and `LocalSimulation` smoothing modes, pawn rollback). Those executions must not touch gameplay state outside the bound set.

```cpp
void UMyMovementCmp::GenPredictionTick_Implementation(float DeltaTime)
{
    Super::GenPredictionTick_Implementation(DeltaTime);
    const bool bLiveMove = !CL_IsReplaying() && !IsSimulatedMove();
    if (bInput_Dash && DashCooldown <= 0.f)
    {
        DashCooldown = DashCooldownDuration;                      // bound: replays restore it
        Velocity += UpdatedComponent->GetForwardVector() * DashSpeed;
        if (bLiveMove) { PlayDashCosmetics(); }                   // once, locally
        CL_DoNotCombineNextMove();                                // seal the move that fired
    }
    DashCooldown = FMath::Max(0.f, DashCooldown - DeltaTime);
}
```

Hits, damage and anything else that decides an outcome belong on the server, inside the move that caused them; the owning client predicts the cosmetic half only.

## Time

- `GetTime()` (`UGMC_ReplicationCmp`) is the server's world clock in seconds. On a client it is the synchronized copy kept by `AGMC_PlayerController` (`GetSyncedWorldTimeSeconds()`, `Public/Actors/GMCPlayerController.h`), latency already accounted for.
- `GetMoveTimestamp()` (`UGMC_MovementUtilityCmp`) is the timestamp of the move being executed. On the server, inside a remote move, it is the *client's* time for that move, so `GetTime() - GetMoveTimestamp()` is the move's age (roughly half a round trip plus buffering). On the owning client it is the time the move was created; during a replay it is the historical value. Use it, not the current time, for anything that must be reproducible across client, server and replay.
- The clock is synchronized **only if exactly one `AGMC_WorldTimeReplicator`** (`Public/Replication/WorldTime.h`, an `AInfo`) exists in the world. Nothing spawns it for you: spawn it from the game mode on the server. It replicates the server's real time every `WorldTimeUpdateInterval` (0.01 s) and the client controller re-syncs on each receipt. Without it, a client's clock starts at its own world start and is never corrected; move timestamps then live in different domains on the two machines, and the log carries a `LogGMCReplication` line that no such actor was found. Two of them is an error.
- Check with `gmc.LogNetWorldTime` and `gmc.StatPing` (see the diagnostics reference).

## Smoothing and sim proxies

- A simulated proxy displays the past: the states received from the server are buffered and interpolated at a simulation delay (`gmc.ShowSimulationDelay`). Bound values with a `SimulationMode` other than `None` ride along and are interpolated with their bound function; values bound `None` never arrive.
- **`PostSpawnSmoothingPause`** (`float`, default `1.f`, category *Networking|Smoothing*) delays all smoothing after a simulated proxy spawns. For that long the actor exists, ticks and is visible, but every bound value still holds its constructor default. Anything a simulated proxy must know at spawn (team, archetype, loadout, identity) must therefore also travel by ordinary actor replication (initial-only is enough) and seed the bound copy from it. The owning client is in a similar position for server-authoritative bound values until its first server update arrives.
- GMC can roll *other* pawns back to the executing pawn's view time while a move runs (`bRollBackServerPawns`, `bRollBackClientPawns`, both default true; `OnPawnRolledBack`). This is why hooks fire with historical states on pawns that are not the one moving.
- The displayed transform of a simulated proxy lags the authoritative one, so local distance checks between a predicted pawn and a simulated proxy compare *now* with *then*. Decide outcomes on the server.

## Detecting state changes

`OnSyncDataApplied(const FGMC_PawnState& State, EGMC_NetContext Context)` fires each time GMC loads a state into the pawn: before and after a local move, when a server state is adopted, before and after *each replayed move* (`LocalClientPawn_PreReplayMoveExecution` / `LocalClientPawn_PostReplayMoveExecution`), around remote-move execution on the server, on every smoothing step of a simulated proxy (`RemoteClientPawn_Simulation`), and during rollback swaps (`RollbackSwap`). The contexts are in `Public/Replication/NetTypes.h`. During a replay the values are historical and the hook can fire many times per frame, so "the death flag just became true" seen there may be a state from 100 ms ago that is about to be re-executed.

Detect transitions once per frame instead: keep an unbound `Last*` copy next to each bound value and compare in `WorldTickEnd_Implementation(float DeltaTime)`, which runs on every machine after all moves, replays and smoothing for the frame are done. Handle both directions: a correction can flip a predicted flag back.

```cpp
void UMyMovementCmp::WorldTickEnd_Implementation(float DeltaTime)
{
    Super::WorldTickEnd_Implementation(DeltaTime);
    if (bIsDead != bLastSeenDead) { bLastSeenDead = bIsDead; OnDeathStateChanged(bIsDead); }
}
```

Use `OnSyncDataApplied` only for work that must agree with the applied state at that instant, such as re-deriving an unbound cache from bound values, and check `Context` before doing anything else.

## Reading the GMC headers

GMC is a licensed plugin: read it inside the project, cite names and paths, never copy its code into other repositories or skills. Paths are relative to the GMC plugin root; the documentation is at https://grimtec.net/gmcv2-doc-contents.

| Fact | Header |
|---|---|
| binding functions, `BindReplicationData`, `PreLocalMoveExecution`, `WorldTickEnd`, `OnSyncDataApplied`, role queries, `CL_IsReplaying`, `SV_IsExecutingRemoteMoves`, `CL_DoNotCombineNextMove`, `GetTime`, `PostSpawnSmoothingPause`, `MaxCombinedDeltaTime`, `ClientSendRate`, `MoveHistoryMaxSize`, smoothing settings | `Source/GMCCore/Public/Components/GMCReplicationComponent.h` |
| `GenPredictionTick`, `GenSimulationTick`, `GenAncillaryTick`, `GetMoveTimestamp`, `GetMoveDeltaTime`, `IsSimulatedMove`, `IsLocalMove`, `IsCombinedMove`, `IsSubSteppedIteration` | `Source/GMCCore/Public/Components/GMCMovementUtilityComponent.h` |
| `EGMC_PredictionMode`, `EGMC_CombineMode`, `EGMC_SimulationMode` | `Source/GMCCore/Public/Replication/SyncSettings.h` |
| `EGMC_InterpolationFunction`, `EGMC_SmoothingMode` | `Source/GMCCore/Public/Replication/Smoothing.h` |
| `EGMC_NetContext` | `Source/GMCCore/Public/Replication/NetTypes.h` |
| `AGMC_WorldTimeReplicator` | `Source/GMCCore/Public/Replication/WorldTime.h` |
| `GetSyncedWorldTimeSeconds`, time-sync internals | `Source/GMCCore/Public/Actors/GMCPlayerController.h` |
| log categories | `Source/GMCCore/Public/Utility/GMCLogCategory.h` |
| console variables | `Source/GMCCore/Private/Components/GMCReplicationComponent.cpp` (top), `Private/Actors/GMCPlayerController.cpp`, `Private/Components/GMCMovementUtilityComponent.cpp`, `Private/Components/GMCOrganicMovementComponent.cpp` |

## Checklist

- Every value the prediction tick reads across moves, or that a replay must restore, is bound in `BindReplicationData_Implementation`, after `Super`, in a stable order; nothing is bound anywhere else.
- Inputs are `ClientAuth_Input` + `CombineIfUnchanged`; server-owned state is `ServerAuth_*`; values simulated proxies need have a `SimulationMode` other than `None`.
- The prediction tick depends only on bound state and its `DeltaTime`: no world time, frame delta, unbound members or unseeded randomness.
- Side effects run only when `!CL_IsReplaying() && !IsSimulatedMove()` on the owner and `!IsSimulatedMove()` on the server; outcomes are decided on the server.
- Every one-shot event raised inside a move is followed by `CL_DoNotCombineNextMove()`.
- Cosmetics for simulated proxies live in `GenSimulationTick` or react to bound values; nothing gameplay-relevant runs there.
- Exactly one `AGMC_WorldTimeReplicator` is spawned (game mode, server) before move timestamps are compared across machines.
- Data a simulated proxy needs at spawn also replicates as an ordinary actor property (`PostSpawnSmoothingPause`).
- Transitions are detected in `WorldTickEnd`, not in `OnSyncDataApplied`.
- Behavior was checked under networked PIE with a client, not only standalone (`gmas:gmas-testing`); corrections inspected with `gmc.ShowClientCorrections` and `gmc.LogClientReplay`.
