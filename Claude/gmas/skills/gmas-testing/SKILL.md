---
name: gmas-testing
description: Use when writing or running automation tests for GMC/GMAS gameplay - headless GMAS specs or networked play-in-editor tests with a dedicated server and clients.
---

> Behavior below was verified against GMC 2.3.x and GMAS 1.4. Confirm signatures in this project's own headers (paths are relative to each plugin's root, e.g. `Source/GMCCore/Public/Components/GMCReplicationComponent.h`); GMC's source is not part of this plugin.

GMAS gameplay is tested at three depths: the pure structs, the ability component driven through a stub movement component without a world, and the real move loop in a networked play-in-editor (PIE) session. The authoring skills (`gmas:gmas-ability`, `gmas:gmas-effect`, `gmas:gmas-attribute`, `gmas:gmas-task`) say which spec to write for each class; this skill is the mechanics: the spec harness and its limits, the headless run, and a PIE harness that starts a dedicated server plus clients, waits until they fly, asserts inside confirm windows and tears the session down. The harness code is in [references/net-test-skeleton.md](references/net-test-skeleton.md). What a healthy session logs and how to read a bad one: `gmas:gmas-debug`. GMAS paths below are relative to `Source/GMCAbilitySystem/`; engine paths to `Engine/Source/`.

## Three layers

| Layer | What runs | Proves | Cannot prove |
|---|---|---|---|
| **1. Structs** | `FAttribute`, `FGMCAttributeModifier`, `FAttributeClamp`, your own math; no `UObject` graph beyond a rooted effect instance | arithmetic, modifier layering, clamp resolution, temporal history | anything that needs the component, a move or a network |
| **2. Component through a stub** | a `UGMC_AbilitySystemComponent` whose `GMCMovementComponent` is an empty `UGMC_MovementUtilityCmp` subclass, both `NewObject` without owner or world; `GenPredictionTick` and the `…ForTest` seams drive it | activation gates, lifecycle and end paths, cost and cooldown, effect application and expiry, tags, task payload dispatch, server-operation routing | prediction, confirmation, replay, combined moves, RPCs, heartbeats, smoothing, actor spawning, anything about *two* machines |
| **3. Real move loop** | PIE with a dedicated server and one or more clients in one process, network emulation on the clients | that owner and server agree after a round trip, that others see it, that nothing fires twice, that respawn and death transitions hold | fine-grained arithmetic (slow, noisy): push that down to layer 1 or 2 |

Which layer for which claim: "the value is X after N ticks" → 1 or 2; "the gate refuses when the tag is set" → 2; "the server accepts the client's spend without a correction", "the shot exists once on every machine", "the effect reaches the other client", "the ability survives 1 % loss" → 3. Every checklist line in the authoring skills that says *verified under networked PIE* is a layer-3 claim; a green layer-2 spec with a red PIE session puts the fault in the network path (`gmas:gmas-debug`). Standalone PIE proves nothing about prediction: its pawn is a locally controlled server pawn, never predicted, replayed or smoothed (`gmas:gmc-prediction`).

## GMAS specs

The shipped specs (`Private/Tests/`, 449 cases under `GMAS.*`, 1.4+) are the model. A spec is a `BEGIN_DEFINE_SPEC` block whose members are the harness, a `Define()` with `BeforeEach` / `AfterEach` / `Describe` / `It`, guarded by `#if WITH_AUTOMATION_WORKER` (GMAS) or `WITH_AUTOMATION_TESTS` (`Runtime/Core/Public/Misc/AutomationTest.h`). Put yours in your game module's `Private/Tests/` or in a test module (`UncookedOnly`) that depends on `GMCAbilitySystem`, `GMCCore` and `GameplayTags`.

```cpp
BEGIN_DEFINE_SPEC(FMyAbilitySpec, "MyGame.Unit.Ability",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
    UMyTestMovementCmp*          MoveCmp     = nullptr;   // UCLASS() class UMyTestMovementCmp : public UGMC_MovementUtilityCmp {}: empty
    UGMC_AbilitySystemComponent* AbilityComp = nullptr;
    UGMCAttributesData*          Attributes  = nullptr;   // your real rows: same clamp, same bGMCBound
END_DEFINE_SPEC(FMyAbilitySpec)

void FMyAbilitySpec::Define()
{
    BeforeEach([this]()
    {
        MoveCmp     = NewObject<UMyTestMovementCmp>(GetTransientPackage());          MoveCmp->AddToRoot();
        AbilityComp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage()); AbilityComp->AddToRoot();
        Attributes  = NewObject<UGMCAttributesData>(GetTransientPackage());          Attributes->AddToRoot();
        // rows: Attribute.MaxStamina 100 / Clamp.Max 1000; Attribute.Stamina bStartFull, MaxAttributeTag = Attribute.MaxStamina
        AbilityComp->GMCMovementComponent = MoveCmp;
        AbilityComp->AttributeDataAssets.Add(Attributes);
        AbilityComp->BindReplicationData();                 // instantiates and binds the attributes; nothing consumes the binds
        AbilityComp->ActionTimer = -1.0;                    // non-zero: effect ids are ActionTimer x 100, and 0 fails every apply
        GetMutableDefault<UMyAbility_Dash>()->CooldownTime = 5.f;   // the ASC instantiates from the CDO: configure it here...
    });
    AfterEach([this]()
    {
        GetMutableDefault<UMyAbility_Dash>()->CooldownTime = 0.f;   // ...and restore it, or the next spec inherits it
        AbilityComp->RemoveFromRoot(); MoveCmp->RemoveFromRoot(); Attributes->RemoveFromRoot();
        AbilityComp = nullptr; MoveCmp = nullptr; Attributes = nullptr;
    });
    Describe("Dash", [this]() { It("spends stamina once", [this]() { /* activate, drive, assert */ }); });
}
```

**The stub.** An ownerless component reports `NM_Standalone` from `GetNetMode()` and `false` from `HasAuthority()`, which is exactly what makes the harness cheap and what bounds it: no net role, no moves, no world. `UGMAS_TestMovementCmp` (`Private/Tests/UGMAS_TestMovementCmp.h`, 1.4+) is that empty subclass; write your own in your module rather than including a plugin-private header.

**Test classes.** The shipped stubs (`UGMAS_TestAbility`, `UGMAS_TestAbilityB`, `UGMAS_TestCostEffect`, same folder) are empty subclasses whose configuration is written to the class default object per test: `GetMutableDefault<UMyAbility_Dash>()->AbilityTag = …`, `GetMutableDefault<UMyEffect_Burn>()->EffectData.Modifiers = …`, with every field restored in `AfterEach`. Testing your real classes the same way works as long as they read their configuration from class defaults and not from assets that exist only in a cooked map.

**Driving the component.** Pick the call by what you need to advance:

| Call | Advances | Caveat |
|---|---|---|
| `TryActivateAbility(Class)`, `TryActivateAbilitiesByInputTag(InputTag, …)` after `AddAbilityMapData(FAbilityMapData)` | activation, gates, candidate order | `QueueAbility` returns silently here: the component is neither an autonomous proxy nor the authority |
| `GenPredictionTick(Dt)` | the operation slot, ability ticks, effect ticks, `ProcessAttributes`, purge of ended instances | **overwrites `ActionTimer`** from the stub's `GetMoveTimestamp()` (`Source/GMCCore/Public/Components/GMCMovementUtilityComponent.h`), which is `-1.0` on a component that never executed a move. Use `GenPredictionTick(0.f)` to purge, never to pass time |
| write `ActionTimer`, then `TickActiveEffects(Dt)` + `ProcessAttributes(true)` | effect duration, periods, expiry; attribute recalculation | the two sub-steps of the prediction tick with a clock you control (`GMAS_DurationSpec.cpp`). Set `Data.bServerAuth = true` when the clock jumps past `ClientEffectApplicationTimeout` (0.5 s), or the unconfirmed client instance times out |
| the instance's public `Tick(Dt)` / `AncillaryTick(Dt)` | `TickEvent`, `TickTasks`; `AncillaryTickEvent`, `AncillaryTickTasks` | keep the written `ActionTimer` within `ServerConfirmTimeout` (2 s, 1.4+; 1 s before) of activation: `UGMCAbility::Tick` reads the component's plain `HasAuthority()`, sees a client, and cuts an unconfirmed instance with an `[AbilityCut]` **Error** (which also fails the test unless `AddExpectedError` registered it). Raise the protected default in the test ability's constructor when a test needs more time |
| `GenAncillaryTick(Dt, false)` | cooldown expiry, task-payload dispatch for `bActivateOnMovementTick = false` abilities, `OnAttributeChanged` / `OnActiveTagsChanged` | runs no replay logic; safe to call freely |
| `PreLocalMoveExecution()` | moves the oldest queued task payload and operation into their bound slots | one payload per call: `N` payloads take `N` moves |

**Seams (1.4+, `WITH_AUTOMATION_WORKER` only, bottom of `Public/Components/GMCAbilityComponent.h`).** They exist because GMC's role and replay queries are not virtual and several dispatch paths are private. `GMAS_BugFixSpec.cpp` and `GMAS_ClientAuthSpec.cpp` show each in use. They are inspection hatches, not a substitute for layer 3: a forced flag proves the branch runs, not that the network takes it.

| Seam | Opens |
|---|---|
| `bForceAuthorityForTest` | `IsAuthorityForGMASLogic()` answers `true`: server-side dispatch. It does *not* cover the plain `HasAuthority()` in `UGMCAbility::Tick` above |
| `bForceReplayingForTest` | `IsReplayingForGMASLogic()` answers `true`: the replay-skip gates in effect ticking and polling |
| `bForceNoAckClientForTest` | the "no acknowledging client" routing of server operations (the next-tick force) |
| `ServerProcessOperationForTest`, `ProcessOperationForTest`, `EnqueueServerOperationForTest`, `ShouldApplyServerOpImmediatelyForTest` | the private dispatch and routing functions, unchanged |
| `BindServerOpForcedDelegateForTest` | binds the grace-force delegate as `BeginPlay` would, without a world |
| `SeedBoundQueueOperationDataForTest(OperationID)` | a valid base struct in the operation slot when `BindReplicationData()` was not called |
| `GetBoundQueueV2ForTest()` | the bound queue itself: seed client operations and payloads, call the drain helpers |
| `TryActivateClientAuthAbilityForTest`, `CheckActivationTagsForClientAuthForTest` | the client-auth activation path without `QueueAbility`'s role gate |
| `GetProcessedEffectIDsForTest`, `GetEffectHandlesForTest`, `GetEffectFromHandleForTest` | effect ids, answer states and handles |

**Running.** From a project with GMC and GMAS enabled, with the editor closed (the two would share `Saved/` and, for PIE tests, the server port):

```
UnrealEditor-Cmd.exe <Project>.uproject -ExecCmds="Automation RunTests GMAS;Quit" -unattended -nullrhi -log
```

`RunTests` takes `+`-separated patterns matched as substrings of the full test name (`Developer/AutomationController/Private/AutomationCommandline.cpp`): `GMAS.Unit.Effect+MyGame.Unit`; `^` anchors the start, `$` the end, `StartsWith:` is the prefix form, `Group:` expands a named group from the automation settings. `Automation List` prints the names; `Quit` after the run ends the process, so the `;Quit` is what makes the command terminate. Results land in `Saved/Logs/<Project>.log` as one `LogAutomationController` line per test with its result and a summary at the end; `-ReportExportPath=<dir>` writes the JSON and HTML report (`AutomationControllerManager.cpp`). In the editor the same tests run from the Session Frontend's Automation tab, where the spec names form the tree.

**Known failing set (1.4).** About 150 specs fail on the shipped tree: `GMAS.Unit.Attribute*`, `GMAS.Stress.*`, `GMAS.Unit.ModifierMath` and the `GMAS.Unit.Ability` cooldown case. Their helpers build `FAttribute` values with the default clamp, which 1.4 applies as a `[0, 0]` pin (`gmas:gmas-attribute`), so every value reads 0; the runtime is right and the helpers predate it. Filter on your own prefix, or treat those names as expected failures; do not change the clamp to make them pass.

## Networked PIE tests

A networked test is an `IMPLEMENT_SIMPLE_AUTOMATION_TEST` whose `RunTest` enqueues one latent command (`ADD_LATENT_AUTOMATION_COMMAND`) and returns `true`; the command's `Update()` runs once per editor frame until it returns `true`. It is editor-only code: `#if WITH_AUTOMATION_TESTS && WITH_EDITOR`, in a module that links `UnrealEd` for editor targets. The skeleton in [references/net-test-skeleton.md](references/net-test-skeleton.md) is a state machine with these phases:

| Phase | Does | Leaves when |
|---|---|---|
| `Start` | waits while a previous session is still closing, then requests the session with explicit settings | `GEditor->RequestPlaySession` accepted the request |
| `WaitReady` | re-resolves the worlds every frame and polls `IsReady()` | readiness held continuously for the settle time |
| `Run` | re-resolves the worlds, calls `RunStep()` | `RunStep()` returned `true`, or `Fail()` |
| `End` | `GEditor->RequestEndPlayMap()` | at once |
| `WaitClosed` | waits until no PIE world context exists, nulls every cached world, calls `Cleanup()` | closed, or the close timeout |

**Session settings** (`Editor/UnrealEd/Classes/Settings/LevelEditorPlaySettings.h`). `NewObject<ULevelEditorPlaySettings>()` copies the config-loaded defaults, i.e. whatever the user last chose in the Play menu, so set every field the test depends on, never inherit one:

| Set | Why |
|---|---|
| `SetPlayNetMode(EPlayNetMode::PIE_Client)` | a windowless dedicated server plus the clients; `PIE_Standalone` for a one-player session, `PIE_ListenServer` only when the test is about a listen host |
| `SetPlayNumberOfClients(N)` | `N` client worlds; the server is not counted |
| `SetRunUnderOneProcess(true)` | every world in this process, so the test can read server and clients alike |
| `bLaunchSeparateServer = false` | no extra server process |
| `NetworkEmulationSettings = FLevelEditorPlayNetworkEmulationSettings()` then `.CurrentProfile = TEXT("Custom")` | drops the user's profile; `Custom` applies the packet values as set (a named profile loads its own) |
| `.bIsNetworkEmulationEnabled = true`, `.EmulationTarget = NetworkEmulationTarget::Client`, `.OutPackets` / `.InPackets` `MinLatency` = `MaxLatency` = half the round trip each, `PacketLossPercentage` per direction (`Editor/UnrealEd/Classes/Settings/LevelEditorPlayNetworkEmulationSettings.h`) | clients only, so the server stays clean and each client sees the whole round trip; constant latency makes runs comparable |
| `FRequestPlaySessionParams` (`Editor/UnrealEd/Public/PlayInEditorDataTypes.h`): `EditorPlaySettings = Settings`, `GlobalMapOverride = "/Game/Maps/L_TestArena"` | the request holds a plain object pointer and starts a few frames later: keep `Settings` alive in a `TStrongObjectPtr` until the next request; the map is the test's, never the one open in the editor |
| refuse when `GEditor->IsPlaySessionInProgress() \|\| GEditor->IsPlaySessionRequestQueued()` or a PIE world exists | only one session can exist: they all bind the same server port |

**Worlds.** Walk `GEngine->GetWorldContexts()` (`Runtime/Engine/Classes/Engine/Engine.h`), keep contexts with `WorldType == EWorldType::PIE` and a `World()`, and classify by `GetNetMode()`: `NM_DedicatedServer` is the server (`NM_Standalone` in a one-player session), each `NM_Client` is a client in context order (client 1 = index 0). Do this **every frame** in `WaitReady` and `Run`: a `UWorld*` cached across a session boundary dangles the moment the session ends, and the world list changes while clients connect. The same goes for actor pointers: pawns are destroyed on death and travel; re-find the pawn from its player controller every step, and pair a client pawn with its server twin through a replicated identity (`PlayerState->GetPlayerId()`), never through pointers, which never cross worlds.

**Readiness and settle.** Default `IsReady()`: the server world exists, all `N` client worlds exist, every client world has a local player controller with a pawn whose ability component exists, and the server holds the twin of each. Then hold for a settle time (2 s) before the first step, with the timer reset whenever readiness lapses: simulated proxies apply no replicated state for `PostSpawnSmoothingPause` (1 s by default, `Source/GMCCore/Public/Components/GMCReplicationComponent.h`), the owner's bound values arrive only after the server saved its first moves, and the clock sync needs `AGMC_WorldTimeReplicator` (`Source/GMCCore/Public/Replication/WorldTime.h`) to have replicated (`gmas:gmc-prediction`). Readiness has a generous timeout (2 min: the first session compiles shaders and loads the map).

**Confirm windows.** Every assertion about another machine or about a predicted result is *poll until true, or until the window closes, then assert once*. A step that asserts on the first frame tests the frame it happened to run on. The default window (6 s) covers a round trip at 75 ms, the 1 s grace after which the server forces an unacknowledged operation (`gmas:gmas-rules`), the smoothing delay and the 30 Hz client send rate, with margin; a claim that something does **not** happen (no second shot, no correction) waits the whole window and asserts at the end.

**Serial execution.** PIE tests run one at a time. The `Start` phase waits for the previous session to close and the `Automation` command runs tests sequentially; never start a second editor on the project meanwhile, and keep one session per test (new options → new test). They run under the same headless command as the specs, narrowed to their prefix (`Automation RunTests MyGame.PIE.Networked;Quit`); `-nullrhi` is fine, the PIE worlds tick without rendering.

**Emulation.** 75 ms round trip and 1 % loss for every prediction claim: without latency the client's move reaches the server before the next frame and nothing is ever corrected, and without loss the unreliable paths (cosmetic multicasts, forced operations) are never exercised. 0 / 0 for pure replication and identity claims, so a flaky result there is a bug and not weather. Test with two clients when the claim involves a simulated proxy.

**Housekeeping.** `Fail(Message)` records the error and moves to `End` so the session closes for the next test; it returns `true` so a step can `return Fail(...)`. Register expected `Error` lines with `AddExpectedError` *before* the action that logs them: an unexpected `Error` log fails the test by itself. Freeze or park AI and other actors that would race the assertion (a bot dying inside the window changes every count). Timeouts fail loudly (`ReadyTimeout`, `RunTimeout`, `CloseTimeout`); a hung session is a failed test, not a hung run.

## What to assert for prediction

- **Owner and server agree.** After the owner's action and inside the window: the client pawn's `GetAttributeValueByTag` equals its server twin's; `HasActiveTag` matches; `GetActiveAbilityCountByTag` matches (both `0` for a one-shot that ended, both `1` for a held ability). A disagreement that persists past the window is the server refusing the prediction or an unbound dependency (`gmas:gmas-debug`).
- **Others see the effect.** On a second client, the simulated proxy shows the bound tag or attribute (through smoothing, so only values whose `SimulationMode` is not `None`) and the cosmetic multicast played; allow the smoothing delay inside the window.
- **Counts.** Spawned actors exist exactly once per world (`TActorIterator<AMyProjectile>` on the server and on each client after the window): one on the owner proves the live-move guard, one on the server proves the move executed once, one on the other client proves the replication path. Effects by tag (`GetNumEffectByTag`) and ability instances match on both sides.
- **No replay burst, no cut.** `OnReplayBurstDetected` (1.4+, a dynamic delegate on the client ASC; bind a small rooted `UObject` with a `UFUNCTION` at step 0) never fires during the action, or a per-frame replay counter your movement component keeps (count frames in which `CL_IsReplaying()`, `Source/GMCCore/Public/Components/GMCReplicationComponent.h`, was true inside `GenPredictionTick`) stays at its pre-action value; the window passes with no `[AbilityCut]`, no Warning-level `[TaskDiag]`, no `Effect … Not Confirmed By Server` (`gmas:gmas-debug`). Under 1 % loss accept the occasional single correction; a burst is the failure.
- **Death and respawn.** Kill through the server (`ServerInstantAttribute` on an unbound attribute, `gmas:gmas-effect`), wait in a window until the client's controller holds a *different* pawn, re-resolve, then assert on both sides: the new ASC's attributes at their initial values, `GetActiveAbilities()` empty, no effect of the old life, and the owner's first predicted activation on the new pawn confirmed (no `[AbilityCut]`). Pointers to the old pawn are stale the moment it dies: never hold them across steps.
- **Never on a frame count.** Nothing in a networked session is a fixed number of frames away; every "after" is a window.

## Checklist

- The claim is placed at the lowest layer that can prove it: arithmetic in struct specs, gates and lifecycle through the stub, anything about two machines in networked PIE.
- Layer-2 harness: ownerless stub movement component, `GMCMovementComponent` set, your real `UGMCAttributesData`, `BindReplicationData()`, `ActionTimer` non-zero; CDOs configured in `BeforeEach` and restored in `AfterEach`; time advanced by writing `ActionTimer` and calling the sub-steps, never through `GenPredictionTick(Dt)`.
- `ServerConfirmTimeout` respected or raised in the test ability; expected `[AbilityCut]` lines registered; seams used as inspection, not as proof of networking.
- Headless run with `-ExecCmds="Automation RunTests <prefix>;Quit" -unattended -nullrhi -log`, results read from `Saved/Logs`; the 1.4 known-failing attribute specs excluded by prefix, not fixed in the runtime.
- Networked test: explicit `ULevelEditorPlaySettings` (`PIE_Client`, client count, one process, `Custom` emulation profile, clients-only emulation), `GlobalMapOverride`, settings kept alive, refusal when a session exists.
- Worlds re-resolved every frame; pawns re-found every step; twins paired by replicated identity; nothing cached across the session boundary.
- Readiness = every player has a pawn with an ability component plus its server twin, held for the settle time; every assertion inside a confirm window; negatives wait the whole window.
- 75 ms / 1 % loss for prediction claims, two clients for proxy claims; AI and other racers frozen; expected errors registered before the raise.
- Assertions cover agreement, visibility on others, counts, no burst or cut, death and respawn; the session is ended with `RequestEndPlayMap()` and the test waits for the worlds to disappear.
- Tests run serially; a timeout is a failure with a message naming the phase and step.
