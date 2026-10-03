# GMC diagnostics: console variables and log categories

Names verified against GMC 2.3.x. The variables are defined at the top of the `.cpp` files named below (paths relative to the GMC plugin root) and exist only in builds that keep the console and logging (not Shipping). Set them from the console (`gmc.ShowNetRole 1`), from `-ExecCmds="gmc.ShowNetRole 1"`, or from `[ConsoleVariables]` in `DefaultEngine.ini`. Every value is an integer; `0` disables.

## Replication and prediction (`Source/GMCCore/Private/Components/GMCReplicationComponent.cpp`)

| Variable | Values | What it does |
|---|---|---|
| `gmc.StatNetMovementValues` | 0–3 | Draws the live movement values (last input vector, linear and angular velocity, actor location and rotation, control rotation) of the pawns with the chosen role on screen: 1 authority, 2 autonomous proxy, 3 simulated proxy. |
| `gmc.StatNetContextValues` | 0–3 | Same role selector, but for the networking context: net role, world time, move history size, pending client and server moves, whether a simulated pawn is extrapolating. |
| `gmc.ShowNetRole` | 0/1 | Floats each pawn's net role above it. First thing to turn on when a listen-server or PIE session behaves differently per machine. |
| `gmc.ShowSimulationDelay` | 0/1 | Floats each pawn's current simulation (smoothing) delay above it: how far in the past a simulated proxy is displayed. |
| `gmc.ShowClientCorrections` | 0/1 | On the client, draws the autonomous proxy's state before (red) and after (green) every correction. Frequent red/green pairs mean the predicted logic is not deterministic. |
| `gmc.ShowClientErrors` | 0/1 | On the server, draws the state a remote client reported (red) against the state the server computed (green) for its pawns. |
| `gmc.LogClientReplay` | 0/1 | Logs detailed information whenever a client replay happens (state before and after, the moves re-executed). |
| `gmc.LogSmoothing` | 0/1 | Logs all data involved in smoothing simulated pawns (very chatty). |
| `gmc.LogSmoothingContext` | 0/1 | Logs the context information around smoothing without the per-step data. |
| `gmc.LogClientMoveTrace` | 0/1 | Traces one move's life: created on the client, sent, executed on the server, acknowledged back. Use it to prove whether a move reached the server at all. |
| `gmc.LogNumExecutedRemoteMoves` | 0/1 | Logs, per frame, how many client moves the server executed. Bursts after a latency spike are normal; a steady 0 means the client is not sending. |
| `gmc.LogNumPendingReliableClientPackets` | 0/1 | Logs how many reliable client packets still await acknowledgement each frame; relates to `UseUnreliableClientMovesThreshold`. |
| `gmc.LogDynamicBufferTime` | 0/1 | Logs the dynamic buffer time computed for adaptive buffered interpolation, when that smoothing mode is active. |
| `gmc.LogForcedNetUpdates` | 0/1 | Logs each time the server forces a net update to clients, with the reason: a client correction, a simulated location moving past the update threshold, a bound value whose simulation setting demands it (`PeriodicAndOnChange_*`), or a custom trigger. |
| `gmc.LogRollbackTimeValidation` | 0/1 | Logs whether the server accepted or declined the rollback timings a client attached to its moves. |
| `gmc.ForceFullSyncDataValidation` | 0/1 | Makes the comparison of client and server states continue past the first deviating bound value, so the log names *every* value that differs instead of only the first. Turn on when hunting an unknown desync. |

## Time and ping (`Source/GMCCore/Private/Actors/GMCPlayerController.cpp`)

| Variable | Values | What it does |
|---|---|---|
| `gmc.StatPing` | 0/1 | Shows the local client's ping to the server on screen. Clients only. |
| `gmc.LogNetWorldTime` | 0/1 | Logs the world time on server and client together with a UTC stamp, so the two logs can be lined up to check the clock sync (`AGMC_WorldTimeReplicator`). |

## Movement visualization (`Source/GMCCore/Private/Components/GMCMovementUtilityComponent.cpp`)

| Variable | Values | What it does |
|---|---|---|
| `gmc.ShowMovementVectors` | 0/1 | Draws velocity and acceleration vectors for every pawn. |

## Organic movement only (`Source/GMCCore/Private/Components/GMCOrganicMovementComponent.cpp`)

Only meaningful for pawns whose movement component derives from `UGMC_OrganicMovementCmp`.

| Variable | Values | What it does |
|---|---|---|
| `gmc.StatOrganicMovementValues` | 0/1 | On-screen readout of the organic component's motion values: movement mode, raw and processed input vector, move delta time, total/XY/Z speed, location, rotation, control rotation, velocity, acceleration, force. |
| `gmc.LogOrganicMovementValues` | 0/1 | The same values written to the log. |
| `gmc.ShowFloorSweep` | 0/1 | Draws the floor sweep result (hit location, normal). |
| `gmc.VisualizeBaseEqualization` | 0/1 | Draws the actor base when base equalization applies (moving platforms). |

## GoldSrc movement only (`Source/GMCCore/Private/Components/GMCGoldSrcMovementComponent.cpp`)

Cheat variables (`ECVF_Cheat`), server side, for the GoldSrc-style component.

| Variable | Values | What it does |
|---|---|---|
| `gmc.FlyMode` | 0/1 | Removes gravity from the pawn. |
| `gmc.NoClipMode` | 0/1 | Removes the pawn's collision and allows free movement in every direction. |

## Log categories (`Source/GMCCore/Public/Utility/GMCLogCategory.h`)

All five default to `Log` verbosity and compile up to `All`; raise one with `Log LogGMCReplication VeryVerbose` (console) or `[Core.Log]` in `DefaultEngine.ini`.

| Category | Covers |
|---|---|
| `LogGMCPawn` | pawn setup and lifetime (`AGMC_Pawn`) |
| `LogGMCController` | player and AI controllers (`AGMC_PlayerController`, `AGMC_AIController`) |
| `LogGMCReplication` | move replication, corrections, replays, smoothing, forced updates, rollback validation, and the notice that no (or more than one) `AGMC_WorldTimeReplicator` exists; the `gmc.Log*` variables above write here |
| `LogGMCMovement` | the movement components' own logic (utility, organic, GoldSrc); `VeryVerbose` traces every executed move with its timestamp, delta time, sub-step and simulated flags |
| `LogGMCRollbackActor` | rollback actors and platforms (`AGMC_RollbackActor`, `AGMC_RollbackPlatform`) |

## Which to reach for

| Symptom | Start with |
|---|---|
| the owning client snaps or stutters | `gmc.ShowClientCorrections`, `gmc.LogClientReplay`, then `gmc.ForceFullSyncDataValidation` to name the deviating value |
| the server disagrees with a client | `gmc.ShowClientErrors`, `gmc.StatNetMovementValues 1` on the server and `2` on the client |
| simulated proxies jitter, lag or teleport | `gmc.ShowSimulationDelay`, `gmc.LogSmoothingContext`, `gmc.LogDynamicBufferTime` |
| a move never seems to reach the server | `gmc.LogClientMoveTrace`, `gmc.LogNumExecutedRemoteMoves`, `gmc.LogNumPendingReliableClientPackets` |
| timestamps or move ages look wrong | `gmc.LogNetWorldTime`, `gmc.StatPing`, and the `LogGMCReplication` notice about `AGMC_WorldTimeReplicator` |
| a rare bound flag is not seen by simulated proxies | `gmc.LogForcedNetUpdates` (and bind it `PeriodicAndOnChange_*`) |
| who is who in a listen-server session | `gmc.ShowNetRole` |
