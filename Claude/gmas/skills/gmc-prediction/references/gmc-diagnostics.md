# GMC diagnostics: console variables and log categories

Names verified against GMC 2.3.x. The variables are defined at the top of the `.cpp` files named below (paths relative to the GMC plugin root) and exist only in builds that keep the console and logging (not Shipping). Set them from the console (`gmc.ShowNetRole 1`), from `-ExecCmds="gmc.ShowNetRole 1"`, or from `[ConsoleVariables]` in `DefaultEngine.ini`. Every value is an integer; `0` disables.

## Replication and prediction (`Source/GMCCore/Private/Components/GMCReplicationComponent.cpp`)

| Variable | Values | What it does |
|---|---|---|
| `gmc.StatNetMovementValues` | 0–3 | Draws the live movement values (last input vector, linear and angular velocity, actor location and rotation, control rotation) of the pawns with the chosen role on screen: 1 authority, 2 autonomous proxy, 3 simulated proxy. |
| `gmc.StatNetContextValues` | 0–3 | Same role selector, but for the networking context: net role, world time, move history size, pending client and server moves, whether a simulated pawn is extrapolating. |
| `gmc.ShowNetRole` | 0/1 | Labels every pawn in the world with the role it has on *this* machine. First thing to turn on when a listen-server or PIE session behaves differently per machine. |
| `gmc.ShowSimulationDelay` | 0/1 | Labels each pawn with how far behind the newest received state it is being drawn (its smoothing delay). Large or oscillating numbers explain "laggy" proxies. |
| `gmc.ShowClientCorrections` | 0/1 | Marks every correction the local pawn receives: where the client had predicted it (red) against where the server put it (green). Frequent pairs mean the predicted logic is not deterministic. |
| `gmc.ShowClientErrors` | 0/1 | The server-side mirror: for each remote pawn, where the client claimed it was (red) against where the server computed it (green). |
| `gmc.LogClientReplay` | 0/1 | Turn on while reproducing a snap: each replay then writes what the client had, what the server sent and which history entries were re-run, so the bound value that started it can be named. |
| `gmc.LogSmoothing` | 0/1 | Dumps every number the smoothing of simulated pawns works with, every frame. A firehose; use for a few seconds. |
| `gmc.LogSmoothingContext` | 0/1 | The surrounding picture (which states and times smoothing chose) without the per-frame dump; the usual first step for a jittery proxy. |
| `gmc.LogClientMoveTrace` | 0/1 | Follows individual moves around the round trip (built on the client, sent, run on the server, answered) to prove whether moves arrive at all and how late. |
| `gmc.LogNumExecutedRemoteMoves` | 0/1 | Per-frame count of received client moves the server ran. Bursts after a latency spike are normal; a steady zero means nothing is arriving. |
| `gmc.LogNumPendingReliableClientPackets` | 0/1 | Per-frame count of the client's move packets the server has not acknowledged yet. A climbing number is congestion; past `UseUnreliableClientMovesThreshold` GMC switches moves to unreliable sends. |
| `gmc.LogDynamicBufferTime` | 0/1 | Shows how the adaptive smoothing mode resizes its buffer over time; only meaningful with `AdaptiveBufferedInterpolation`. |
| `gmc.LogForcedNetUpdates` | 0/1 | Logs each time the server forces a net update to clients, with the reason: a client correction, a simulated location moving past the update threshold, a bound value whose simulation setting demands it (`PeriodicAndOnChange_*`), or a custom trigger. |
| `gmc.LogRollbackTimeValidation` | 0/1 | Reports each decision the server makes about the rollback timing a client attached to a move: taken as is, or replaced by the server's own estimate. |
| `gmc.ForceFullSyncDataValidation` | 0/1 | Makes the comparison of client and server states continue past the first deviating bound value, so the log names *every* value that differs instead of only the first. Turn on when hunting an unknown desync. |

## Time and ping (`Source/GMCCore/Private/Actors/GMCPlayerController.cpp`)

| Variable | Values | What it does |
|---|---|---|
| `gmc.StatPing` | 0/1 | On-screen round-trip time from this client to the server; does nothing on the server. |
| `gmc.LogNetWorldTime` | 0/1 | Writes each machine's world clock next to a wall-clock (UTC) stamp, so a server log and a client log can be aligned and the offset between their clocks measured. The way to confirm `AGMC_WorldTimeReplicator` is doing its job. |

## Movement visualization (`Source/GMCCore/Private/Components/GMCMovementUtilityComponent.cpp`)

| Variable | Values | What it does |
|---|---|---|
| `gmc.ShowMovementVectors` | 0/1 | Draws each pawn's velocity and acceleration as arrows in the world. |

## Organic movement only (`Source/GMCCore/Private/Components/GMCOrganicMovementComponent.cpp`)

Only meaningful for pawns whose movement component derives from `UGMC_OrganicMovementCmp`.

| Variable | Values | What it does |
|---|---|---|
| `gmc.StatOrganicMovementValues` | 0/1 | On-screen readout of the organic component's motion values: movement mode, raw and processed input vector, move delta time, total/XY/Z speed, location, rotation, control rotation, velocity, acceleration, force. |
| `gmc.LogOrganicMovementValues` | 0/1 | The same values written to the log instead of the screen. |
| `gmc.ShowFloorSweep` | 0/1 | Draws what the ground probe under the pawn hit. |
| `gmc.VisualizeBaseEqualization` | 0/1 | Draws the surface the pawn stands on as the component treats it once base equalization applies (moving platforms). |

## GoldSrc movement only (`Source/GMCCore/Private/Components/GMCGoldSrcMovementComponent.cpp`)

Cheat variables (`ECVF_Cheat`), server side, for the GoldSrc-style component.

| Variable | Values | What it does |
|---|---|---|
| `gmc.FlyMode` | 0/1 | Switches gravity off for the pawn. |
| `gmc.NoClipMode` | 0/1 | Turns the pawn into a ghost: collision off, free flight along every axis. |

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
