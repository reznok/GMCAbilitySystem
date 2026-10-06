---
name: gmas-upgrade
description: Use when moving a project to a newer GMAS (for example 1.3 to 1.4, or 1.4 to 1.4.1) or when a build breaks after updating the GMAS submodule.
---

> Behavior below was verified against GMC 2.3.x and GMAS 1.4. Confirm signatures in this project's own headers (paths are relative to each plugin's root, e.g. `Source/GMCCore/Public/Components/GMCReplicationComponent.h`); GMC's source is not part of this plugin.

A GMAS upgrade is three jobs: data that silently changes meaning (clamps, flipped defaults), code that stops compiling (the bound queue, apply signatures, removed forwarders), and behaviour that compiles but acts differently (activation tick, tag preservation, timing). The tables below are built from the public header diff between the two release tags and the release notes; every old/new pair was checked against `git show v1.3.0:<path>` and the 1.4 headers. GMAS paths are relative to `Source/GMCAbilitySystem/`. The rules the new behaviour follows live in `gmas:gmas-rules`; the install mechanics in `gmas:gmas-setup`.

## Find the installed generation

| Signal | Where | Reads |
|---|---|---|
| `VersionName` | `GMCAbilitySystem.uplugin` at the plugin root | `"1.3"`, `"1.4"`; release tags are `v1.3.0`, `v1.4.0` (`git -C Plugins/GMCAbilitySystem describe --tags` for a submodule) |
| the queue header | `Public/Utility/GMASBoundQueueV2.h` present ⇒ 1.4+; `Public/Utility/GMASBoundQueue.h` present ⇒ 1.3 or older | the two never coexist |
| the hook | this plugin's SessionStart hook prints `VersionName` and `1.4+ bound queue V2` or `pre-1.4: bound queue V1` for the GMAS it finds under `Plugins/` | |
| the test module | `Source/GMCAbilitySystemTests/` present ⇒ 1.4.1+ (`VersionName` stays `"1.4"` for patch releases) | `git -C Plugins/GMCAbilitySystem describe --tags` names the patch |

Everything the other `gmas:*` skills tag `(1.4+)` applies only after the move; a `dev` checkout between tags is "1.4+" with unreleased changes on top (`git -C Plugins/GMCAbilitySystem log --oneline v1.4.0..HEAD -- Source/GMCAbilitySystem/Public`).

## 1.3 → 1.4

Access is the C++ visibility on the 1.3 header; `private virtual` members were overridable from an ability-system-component subclass, so they are listed.

**Bound queue** (`Public/Utility/`, `Public/Components/GMCAbilityComponent.h`):

| 1.3 | 1.4 | What to change |
|---|---|---|
| `Utility/GMASBoundQueue.h`: `TGMASBoundQueue<C, T, ClientAuth>`, `TGMASBoundQueueOperation<C, T>`, `EGMASBoundQueueOperationType`, `FGMASBoundQueueRPCHeader`, `FGMASBoundQueueAcknowledgement(s)`, `FGMASBoundQueueOperationIdSet`, `FGMASBoundQueueEmptyData` | the header is gone | delete the include and every template instantiation. Game code should not touch the queue: `QueueAbility`, `ApplyAbilityEffect*`, `AddImpulse`, `FireCustomEvent` on the component are the API |
| – | `Utility/GMASBoundQueueV2.h`: one untemplated `FGMASBoundQueueV2` per component (`BoundQueueV2`, private). `Utility/GMASBoundQueueV2_Operations.h`: one `USTRUCT` per operation deriving from `FGMASBoundQueueV2OperationBaseData` (`…AbilityActivationOperation`, `…ApplyEffectOperation`, `…RemoveEffectOperation`, `…AddImpulseOperation`, `…SetActorLocationOperation`, `…CustomEventOperation`, `…AcknowledgeOperation`, batch and client-auth variants), carried as `FInstancedStruct` | nothing to write; the struct names matter only to a component subclass overriding `ProcessOperation` (below) |
| the component's queues `QueuedAbilityOperations`, `QueuedEffectOperations`, `QueuedEffectOperations_ClientAuth`, `QueuedEventOperations` (private) with the template's `GetQueuedOperations`, `GetQueuedRPCOperations`, `Num`, `NumMatching`, `GetOperationById`, `BindToGMC`, `PreLocalMovement` / `PreRemoteMovement` | no public equivalent (`BoundQueueV2.OperationQueue`, `ClientQueuedOperations`, `GetPayloadByID` are reachable only through `GetBoundQueueV2ForTest()` under `WITH_AUTOMATION_WORKER`); the private helpers around them (`ClientQueueOperation`, `ClientHandlePendingOperation`, `ServerHandlePendingEffect`, `RPCClientQueueEffectOperation`) went with the template | drop the inspection; diagnose with `GMAS.LogApplyTrace` and the `[ProcessOp]`, `[AckTrace:Client:GenTick]`, `[AckTrace:Server:ProcessAck]` lines (`gmas:gmas-debug`) |
| `CreateEffectOperation(…)`, `CreateSyncedEventOperation(…)` (public) | removed (two commented-out declarations remain) | `ApplyAbilityEffect` with a queue type; `FireCustomEvent` |
| overridable `ProcessOperation(const TGMASBoundQueueOperation<UGMCAbilityEffect, FGMCAbilityEffectData>&)`, `ProcessOperation(const TGMASBoundQueueOperation<UGMASSyncedEvent, FGMASSyncedEventContainer>&)` (private virtual) | `virtual bool ProcessOperation(FInstancedStruct OperationData, bool bFromMovementTick = true, bool bForce = false)`, `virtual void ProcessEffectApplicationFromOperation(const FGMASBoundQueueV2ApplyEffectOperation&)`, `virtual void ServerProcessOperation(const FInstancedStruct&, bool)`, `virtual void ServerProcessAcknowledgedOperation(int, bool)` (private virtual) | re-implement on the instanced struct (`OperationData.GetScriptStruct() == FGMASBoundQueueV2AddImpulseOperation::StaticStruct()`, then `GetPtr<T>()`) and call `Super`: the base handles acknowledgements, batches and the consumed-activation guard |
| `PreRemoteMoveExecution()` (public, `BlueprintCallable`), forwarded by the movement component | removed; `PreLocalMoveExecution()` is the only pre-move hook | delete the forwarder line (or Blueprint node) in `UMyMovementCmp`; the five remaining hooks are listed in `gmas:gmas-rules` |
| `CheckRemovedEffects()`, `ActiveEffectsData` (`ReplicatedUsing = OnRep_ActiveEffectsData`), the members `EffectStatePrediction` and `QueuedEffectStates` (private) | the members are gone (the `FEffectStatePrediction` struct is still declared, unused); predicted effects are validated against the bound active-effect ids (`FGMASActiveEffectIDsState`) and reconnecting clients rehydrate from `FGMCEffectSnapshot`. `TickActiveEffects(float)` and `ProcessAttributes(bool)` survive and are public now (the specs drive them) | nothing to call; code that read `ActiveEffectsData` reads `GetActiveEffects()` |

**Effects** (`Public/Components/GMCAbilityComponent.h`, `Public/Effects/GMCAbilityEffect.h`):

| 1.3 | 1.4 | What to change |
|---|---|---|
| `UGMCAbilityEffect* ApplyAbilityEffect(Class, Data, bool bOuterActivation = false)` ("Apply Ability Effect (Legacy)"; `false` applied `Predicted`, `true` `ServerAuth`) | removed, Blueprint node included | `bool ApplyAbilityEffect(Class, Data, QueueType, int& OutEffectHandle, int& OutEffectId, UGMCAbilityEffect*& OutEffect)`, `ApplyAbilityEffectSafe(…, bool& OutSuccess, …, UGMCAbility* HandlingAbility = nullptr)` or `ApplyAbilityEffectShort(Class, QueueType, HandlingAbility)`; keep `OutEffectId` or the `EffectTag` for removal (`gmas:gmas-effect`) |
| `OutEffectHandle` was a separate handle; `RemoveActiveAbilityEffectByHandle(Handle, QueueType)`, `RemoveEffectByHandle(Handle, QueueType)` | `OutEffectHandle` is an alias of `OutEffectId`; both `*ByHandle` removers are `UE_DEPRECATED(5.7, …)` and `DeprecatedFunction` | `RemoveEffectByIdSafe({Id}, QueueType)` |
| `RemoveEffectByTag(Tag, Num, bOuterActivation)`, `RemoveEffectById(Ids, bOuterActivation)` ("Legacy") | unchanged since 1.3: still compile, `DeprecatedFunction` for Blueprint only, no `UE_DEPRECATED`, so C++ emits no warning | `RemoveEffectByTagSafe(Tag, Num, QueueType)`, `RemoveEffectByIdSafe(Ids, QueueType)` with the queue type the apply used; grep for the callers yourself |
| `EGMCAbilityEffectQueueType::ServerAuthMove`: the server queued the operation into the move cycle with the effect's `ClientGraceTime` as RPC grace | still declared (hidden in the editor), shares the `ServerAuth` case: no separate behaviour | write `ServerAuth`; the operation grace is `QueueServerOperation`'s 1 s (`Public/Utility/GMASBoundQueueV2.h`), 0 for targets without a client |
| – | `ServerInstantAttribute` (1.4+): server-only, applied in the current server tick, `Instant` attribute-only effects, no client instance | for damage and other server-owned stats on unbound attributes (`gmas:gmas-rules`); never for an attribute predicted logic reads |
| `GetActiveEffectsByTag(FGameplayTag, bool)`, `GetFirstActiveEffectByTag(FGameplayTag)`, `RemoveActiveAbilityEffectByTag(FGameplayTag, QueueType, bool)` | `const FGameplayTag&` | plain calls compile; re-bind function pointers and delegates |
| `FGMCAbilityEffectData::operator==` (compared `StartTime` and `EndTime`) | removed | `TArray<FGMCAbilityEffectData>::Contains` / `Find` / `AddUnique` and `==` on the struct stop compiling: compare `EffectID` or `EffectTag` yourself |
| `FGMCAbilityEffectData::IsValid()` true only with modifiers, tags, abilities or must-have / must-not tags | also true with `bUniqueByEffectTag` or an `EffectTag` | runtime-built data start from `UGMCAbilityEffect::GetDefaultEffectData(Class)` (1.4+); a struct carrying only a tag is now "complete" data, not a request for the class defaults |
| `void EndEffect()` (`BlueprintCallable`, non-virtual) | `virtual void EndEffect()` | a subclass that declared its own `EndEffect()` now overrides it and is called from the component: add `override` and call `Super::EndEffect()`, or rename |
| `StartEffectEvent`, `EndEffectEvent`: `BlueprintImplementableEvent` | `BlueprintNativeEvent` | Blueprint graphs keep their event nodes; C++ may override `StartEffectEvent_Implementation` |
| `ClientEffectApplicationTimeout` (private field, 1 s) | `UGMASNetworkTimingSettings::ClientEffectApplicationTimeout` (0.5 s, `Public/Settings/GMASNetworkTimingSettings.h`; Project Settings → GMC Ability System → Network Timing, `DefaultGame.ini`); the field left on the component is dead | a project that raised it by editing GMAS sets the project setting instead |

```cpp
// 1.3: the bool chose the queue.
UGMCAbilityEffect* Burn = ASC->ApplyAbilityEffect(UMyEffect_Burn::StaticClass(), Data, /*bOuterActivation=*/true);
// 1.4: a queue type and out parameters; keep Id for RemoveEffectByIdSafe.
int Handle = -1, Id = -1; UGMCAbilityEffect* Burn = nullptr;
ASC->ApplyAbilityEffect(UMyEffect_Burn::StaticClass(), Data, EGMCAbilityEffectQueueType::ServerAuth, Handle, Id, Burn);
```

**Abilities, tasks and the component** (`Public/Components/GMCAbilityComponent.h`, `Public/Ability/GMCAbility.h`, `Public/Ability/Tasks/GMCAbilityTaskBase.h`):

| 1.3 | 1.4 | What to change |
|---|---|---|
| `void TryActivateAbilitiesByInputTag(const FGameplayTag&, const UInputAction* = nullptr, bool bFromMovementTick = true)` | `bool …(…, const bool bFromMovementTick = true, const bool bForce = false, const int SourceOperationID = 0)` | calls compile; the return says whether a candidate activated. Server-only and test code; never from a client (`gmas:gmas-rules`) |
| `bool TryActivateAbility(TSubclassOf<UGMCAbility>, const UInputAction* = nullptr, const FGameplayTag ActivationTag = EmptyTag)` | `+ bool bSkipActivationTagsCheck = false, const int ForcedAbilityID = 0` | calls compile; leave the new arguments to the component |
| `AddImpulse(FVector, bool bVelChange = false)` private (Blueprint only) | public; a `FGMASBoundQueueV2AddImpulseOperation` | C++ may call it on the server (`gmas:gmas-rules`) |
| `ExecuteSyncedEvent(FGMASSyncedEventContainer)` (`BlueprintCallable` but private in C++ on both trees: only Blueprint graphs could call it), `UGMASSyncedEvent` subclasses, `OnSyncedEvent` | declared, but it validates its input and queues nothing | `FireCustomEvent(EventTag, Payload)` on the server, handled in `OnCustomEvent(EventTag, Payload)` on both sides; `SetActorLocation(FVector)` for the teleport case; port each `UGMASSyncedEvent` body into a handler keyed by tag |
| `UGMCAbility::ServerConfirmTimeout` (private, 1 s) | protected, `EditDefaultsOnly`, 2 s | set it in class defaults where a class needs another window |
| `TickEvent`, `AncillaryTickEvent`, `BeginAbilityEvent`, `EndAbilityEvent`: `BlueprintImplementableEvent` | `BlueprintNativeEvent` | Blueprint graphs unchanged; C++ abilities override `BeginAbilityEvent_Implementation` and friends instead of `BeginAbility` (`gmas:gmas-ability`) |
| `UGMCAbilityTaskBase`: no `OnDestroy` override; heartbeat fields `float`, `HeartbeatInterval` 0.2 s of `ActionTimer` | `virtual void OnDestroy(bool) override` removes the task from `RunningTasks`; `double`, 1 s of real time | a task that overrides `OnDestroy` must call `Super::OnDestroy(bInOwnerFinished)` or it stays registered and keeps heartbeating (`gmas:gmas-task`) |
| `SpawnParticleSystemAtLocation(SpawnParams, bool bIsClientPredicted = false, bool bDelayByGMCSmoothing = false)` | `(SpawnParams, const TArray<FGMASNiagaraUserParam>& UserParams, bool, bool)`; `+ SpawnParticleAtPoint`, `PlayCameraShakeAtLocation` | a C++ call `(Params, true)` fails: insert `{}` (or real params from `Utility/GMASNiagaraParams.h`); Blueprint nodes gain a `User Params` pin |
| `GetActiveTags()` returned the bound container | returns the union of bound and client-auth tags; `GetBoundActiveTags()` is the validated set | nothing unless `ClientAuthorizedAbilities` / `ClientAuthorizedAbilityEffects` are used; gates read the union (`gmas:gmas-rules`) |
| `FAbilityMapData` `USTRUCT()`, `EditDefaultsOnly`; `AddAbilityMapData(const FAbilityMapData&)` private | `BlueprintType`, `EditAnywhere`; the struct overload is public | nothing |

**Attributes** (`Public/Attributes/`):

| 1.3 | 1.4 | What to change |
|---|---|---|
| `FAttributeClamp { Min, Max, MinAttributeTag, MaxAttributeTag }`; `IsSet()` false when all four were zero / empty, and then no clamp at all | `+ bClampMin`, `bClampMax` (both `true`); `IsSet()` is either flag; each active bound applies on its own; `operator==` compares the flags | **every attributes DataAsset row that left `Clamp` untouched is now pinned at 0.** Set `Max` or `MaxAttributeTag`, or untick `bClampMax`, and the same for the floor (`gmas:gmas-attribute`). Code that built `FAttributeClamp{}` as "unclamped" sets both flags `false` |
| `EModifierType`: `Add` and the `AddPercentage*` family | `+ Set`, `SetReplace`, `AddPercentageOfBase` (appended: saved assets keep their ops) | nothing to port; the layering order is in `gmas:gmas-attribute` |
| `EGMCAttributeModifierType`: `AMT_Value`, `AMT_Attribute`, `AMT_Custom` | `+ AMT_External` with `ExternalTag`, `ExternalValueIndex`, resolved by the virtual `GetExternalModifierValue(const FGameplayTag&, int32)` on the component | nothing unless you want it |
| `FGMCAttributeModifier`: `AttributeTag` declared first, `Op` after it | `Op` declared first, then `AttributeTag`; `+ Conditions` (`TArray<FGMCModifierCondition>`), `ExternalTag`, `ExternalValueIndex` | positional or designated initialisers of the struct: check the field order; named member assignment is unaffected |
| `FAttributeData` | `+ bStartFull`, `ValueCombineMode` (`EGMC_CombineMode`, from GMC's `Replication/SyncSettings.h`) | nothing; `bStartFull` replaces "DefaultValue = the max" rows |
| `FAttribute::ValueTemporalModifiers` was a `UPROPERTY()` (replicated with `UnBoundAttributes`) | no `UPROPERTY`: never replicated | clients of an unbound attribute see `Value` and `RawValue` only; nothing may read a client-side history |
| `GMCAttributeModifierCustom_Base.h` included `GMCAbilitySystem.h` | include removed | a translation unit that got `LogGMCAbilitySystem` through that header includes `GMCAbilitySystem.h` itself |

**Dependencies and includes:**

| 1.3 | 1.4 | What to change |
|---|---|---|
| `GMCAbilitySystem.uplugin` plugins: `GMC`, `StructUtils` | `+ Niagara` (the module already linked `Niagara` privately) | Niagara is on by default; a project that disabled it must enable it again or the plugin fails to load. Game modules need `Niagara` in their `Build.cs` only when they handle `UNiagaraComponent` themselves |
| `#include "InstancedStruct.h"` | `StructUtils/InstancedStruct.h` behind `ENGINE_MINOR_VERSION >= 5` guards in `GMCAbility.h`, the task headers and `GMASBoundQueueV2_Operations.h`; unguarded in `Utility/GMASBoundQueueV2.h` | your own includes: the same guard, or the new path on 5.5+. On 5.4 expect a missing-header error at `GMASBoundQueueV2.h:3` and fix it upstream, not in the submodule |
| the component header included `Containers/Deque.h` | it does not | include it where you used `TDeque` through it |
| `Build.cs` | `+ DeveloperSettings`, `UMG` (private); `Private/Tests` added to `PublicIncludePaths` | nothing; do not include the now-visible `UGMAS_Test*` headers from game code (1.4.1 moves them into a test module) |

**Compiles, behaves differently:**

- `bActivateOnMovementTick` default `false` → `true` (`Public/Ability/GMCAbility.h`): for every ability class that never set it, activation and task-payload dispatch move from the ancillary tick (never replayed) to the prediction tick (replayed); the ability's own `Tick` already ran in the prediction tick on both versions. Set it `false` explicitly on once-only abilities (weapon fire) and keep one value per input tag: the first granted candidate decides for the batch, a mismatch logs an `Error` (`gmas:gmas-ability`).
- `bPreserveGrantedTagsIfMultiple` default `false` → `true`, and the test changed from "another live effect with the same `EffectTag`" to "any other live effect that still grants the tag" (`RemoveTagsFromOwner`, `Private/Effects/GMCAbilityEffect.cpp`). An effect whose end was meant to strip a tag another effect also grants sets it `false`.
- Clamps: 1.3 ignored an all-zero clamp, 1.4 applies it. Attributes read 0 on both sides from the first frame until the rows are fixed; the debugger's Attributes row shows it at once (`gmas:gmas-debug`).
- `ServerConfirmTimeout` 1 s → 2 s: an activation the server refuses lives twice as long on the client before `[AbilityCut]`.
- `ClientGraceTime` default 1 → 0 and a new meaning: on 1.3 the RPC grace of a `ServerAuthMove` operation; on 1.4 the bilateral window for ending a removed `Ticking` / `Periodic` effect, `0` meaning `UGMASNetworkTimingSettings::DefaultClientGraceTime` (0.5 s). On the 1.4.0 tree that defer is disabled by a source-level override (`RemoveActiveAbilityEffect`, `Private/Components/GMCAbilityComponent.cpp`: `const bool bHasGracePeriod = false;`), so neither value changes anything there; 1.4.1 restores it (below, `gmas:gmas-effect`).
- `QueueAbility(…, bPreventConcurrentActivation = true)`: 1.3 refused while any live instance was of a class on the row; 1.4 refuses only when every candidate has a live instance, and a class with `bAllowMultipleInstances` never blocks.
- Server operations whose target has no autonomous client (AI, unpossessed pawns) apply on that pawn's next ancillary tick instead of after the 1 s grace; `QueueAbility` on a server-controlled pawn (AI, the listen-server host) is a server operation on 1.4 (`gmas:gmas-rules`). Bots react a second earlier; code that waited for them adjusts.
- Cooldowns are stored as an absolute expiry (`ActiveCooldowns`, `TMap<FGameplayTag, double>`): combined moves no longer drift them. Ability ids derive from the operation id and match on both sides; replayed activations are skipped (`ConsumedActivationOperationIDs`) instead of refused by the cooldown.
- Log levels: the force and effect-application lines that were `Warning` are `Verbose`; a log watcher keyed on them goes quiet (`gmas:gmas-debug`).

**Worth adopting after the move** (all 1.4+): `ServerInstantAttribute` for server-owned stats and `FireCustomEvent` for owner-side gameplay cues (`gmas:gmas-rules`); `bUniqueByEffectTag`, `Conditions`, `Set` / `SetReplace`, `AMT_External` with `GetExternalModifierValue`, `GetDefaultEffectData` (`gmas:gmas-effect`, `gmas:gmas-attribute`); `bStartFull` and `ValueCombineMode` rows (`gmas:gmas-attribute`); chain windows, `bBlockAllOtherAbilities`, `GetAbilityCostValues`, `IsReplayingForGMASLogic()` as the live-move guard (`gmas:gmas-ability`); `OnReplayBurstDetected` and the `GMAS.LogApplyTrace` / `BL.GMAS.DumpAttrBindMap` (`GMAS.DumpAttrBindMap` from 1.4.1) console tools (`gmas:gmas-debug`); the 449 `GMAS.*` specs as models (`gmas:gmas-testing`).

## Procedure

1. **Record the start.** Note the generation (above) and the GMC version (`GMC.uplugin`; 1.4 wants 2.3.x). Read the release notes (`gh release view v1.4.0 --json body -q .body` in a clone, or the GitHub release page) once, end to end.
2. **Move the plugin.** Submodule: `git -C Plugins/GMCAbilitySystem fetch --tags`, `git -C Plugins/GMCAbilitySystem checkout v1.4.0`, commit the pointer (`gmas:gmas-setup`). Copied folder: `diff -r` the old folder against the release it came from first; every local patch goes upstream (or is dropped), then replace the folder wholesale. Never carry patches inside the plugin.
3. **Data before code.** Open every `UGMCAttributesData` asset and give each row a deliberate clamp (table above). List the effect classes that share granted tags and decide `bPreserveGrantedTagsIfMultiple` per class; list the abilities that must run once and set `bActivateOnMovementTick = false` on them. Blueprint classes that never touched a property follow the new native default.
4. **Build.** Regenerate project files, build the editor target. Fix errors top-down with the tables, in this order: includes → bound queue → effects → abilities and the component → attributes. Treat the `UE_DEPRECATED` warnings on the `*ByHandle` removers as work items, not noise, and grep for the legacy `RemoveEffectByTag` / `RemoveEffectById` calls yourself: they carry Blueprint deprecation metadata only and compile silently. Then the Blueprint side: `UnrealEditor-Cmd.exe <Project>.uproject -run=CompileAllBlueprints -unattended -log` (the engine's `UCompileAllBlueprintsCommandlet`) lists every graph still calling "Apply Ability Effect (Legacy)", "Pre Remote Move Execution" or a node whose pins changed; "Execute Synced Event" compiles and does nothing, so grep the log for it separately (`gmas:gmas-testing` for the headless invocation shape).
5. **Specs.** Run the GMAS specs headless (`gmas:gmas-testing`: `-ExecCmds="Automation RunTests GMAS;Quit"`). On the 1.4.0 tree about 150 fail by design: `GMAS.Unit.Attribute*`, `GMAS.Stress.*`, `GMAS.Unit.ModifierMath` and the `GMAS.Unit.Ability` cooldown case (their helpers predate the clamp change). Anything else red is a problem with the tree you pulled. Then run the project's own tests and update the ones that drove the old API (`TickActiveEffects` + `ProcessAttributes(true)` replace hand-rolled move stepping).
6. **PIE with a dedicated server and a client** (`gmas:gmas-testing`). Confirm: attributes non-zero on both sides; a press activates with no `[AbilityCut]` and no `Not Confirmed By Server`; a `ServerAuth` effect on a bot lands on its next tick; no `OnReplayBurstDetected` during ordinary play; `bActivateOnMovementTick` abilities fire once. Symptoms and their lines: `gmas:gmas-debug`.
7. **Re-read `gmas:gmas-rules`** for what the new tree does differently by design: the server-operation funnel and its next-tick path for no-client targets, `ServerInstantAttribute`, the union tag queries, the Network Timing settings and the disabled grace defer. Revisit abilities that were designed around the old behaviour.
8. **Commit** pointer, data and code together, naming the GMAS version in the message.

## 1.4 → 1.4.1

A patch release that fixes the 2026-10-03 source audit (`docs/audits/2026-10-03-source-audit.md` in the GMAS repository). `VersionName` stays `"1.4"`; tell the trees apart by the `v1.4.1` tag or by `Source/GMCAbilitySystemTests/` existing. Every row is old (1.4.0) → new (1.4.1) → what to change.

The changes fall in three tiers. Most of the release is the third: fixes that change behaviour only for code that relied on the bug. Two downstream projects (one on 1.4.0, one on 1.3) built and passed their tests against 1.4.1 without code changes; expect work mainly from tier 2's cancel-versus-end row.

### Tier 1: breaking (compile, link or wire)

A project that touches these fails to build, or a mixed client/server pair stops exchanging task payloads. Each fix is mechanical.

**Wire and build compatibility:**

| 1.4.0 | 1.4.1 | What to change |
|---|---|---|
| `EGMCAbilityTaskDataType { None, Heartbeat, Progress }`: `Progress` = 2 in the bound task payload | `Heartbeat` (never produced) removed: `{ None, Progress }`, `Progress` = 1 | client and server builds must both be 1.4.1 (a mixed pair drops every task payload); code that wrote `Heartbeat` deletes it |
| specs and `UGMAS_Test*` stubs in the runtime module, `Private/Tests` on the public include paths | a separate `GMCAbilitySystemTests` module (`UncookedOnly`); `Private/Tests` gone from the include paths | a project module that included `UGMAS_Test*` or `GMAS_*` headers copies what it needs into its own test module |
| `GameplayDebugger` an unconditional public dependency; `AutomationController` / `AutomationWorker` private | `SetupGameplayDebuggerSupport` (public where the target has the debugger); the automation modules dropped | a module that used the debugger only through GMAS adds `GameplayDebugger` (guarded by `WITH_GAMEPLAY_DEBUGGER`) itself |

**Removed** (no replacement needed unless stated):

| 1.4.0 | 1.4.1 | What to change |
|---|---|---|
| `FEffectStatePrediction` (USTRUCT, unused) | removed | delete references |
| `FGMASQueueOperationHandle` (USTRUCT) and the handle registry behind `GetEffectFromHandle_BP` | removed; handles are effect ids | `GetEffectById(Id)`; the test seams `GetEffectHandlesForTest` / `GetEffectFromHandleForTest` are gone too |
| `UGMCAbilityEffect::CalculatePeriodicTicksBetween` | removed (no callers) | compute periods from `ActionTimer` yourself |
| `UGMCAbilityEffect::ProcessCustomModifier`, `CustomModifiersInstances` | removed: calculators always ran on the class default object | keep custom calculators stateless (configuration in class defaults) |
| `FAttribute::OnAttributeChanged` and the `FAttributeChanged` delegate type (never broadcast) | removed | bind the component's `OnAttributeChanged` or `AddAttributeChangeDelegate` |
| `UGMC_AbilitySystemComponent::GetActiveEffectsDataString()` (returned `UNDER CONSTRUCTION`) and the debugger's *Active Effects Data* row | removed; `UGMCAbilityEffect::ToString()` shows the client answer state (`Pending` / `Validated` / `Timeout` / `Authority`) in the *Active Effects* row | use `GetActiveEffectsString()` or the effect's `ToString()` |

**Signatures:**

| 1.4.0 | 1.4.1 | What to change |
|---|---|---|
| `virtual bool UGMCAbilityEffect::IsPaused()` | `virtual bool IsPaused() const` | make overrides `const`. An override declared without `override` compiles and silently stops overriding; one with `override` fails to compile |
| `FString UGMCAbilityEffect::ToString()` | `FString ToString() const` (now in the `.cpp`) | nothing for callers; a subclass that hid it adds `const` |
| `float UGMCAbility::GetClientStartTime() const` | `double` | store it in a `double` (or cast) |
| `float UGMCAbilityEffect::ClientEffectApplicationTime` | `double` | same |
| `bool UGMCAbilityEffect::DoesOwnerHaveTagFromContainer(FGameplayTagContainer&)` (private) | `const FGameplayTagContainer&` | nothing outside GMAS |
| `FGMASBoundQueueV2::QueueServerOperation(int, float Timeout = 1.0f)` | `QueueServerOperation(int, float Timeout)`: no default; the component passes `UGMASNetworkTimingSettings::ServerOperationGraceSeconds` (new, 1 s) or 0 for pawns without a client | pass a timeout; tune the grace in Project Settings → GMC Ability System → Network Timing |
| `UGMCAbilityTaskBase::IsClientOrRemoteListenServerPawn()` | `DrivesPawnLocally()` (same answer); the old name is deprecated | rename in task subclasses |

### Tier 2: semantics changes (review your abilities)

Deliberate new behaviour. Builds unchanged, but gameplay code written against 1.4.0 may need adjusting; the cancel-versus-end row is the one most projects must audit.

| 1.4.0 | 1.4.1 | What to change |
|---|---|---|
| abnormal and external ends were natural ends (`EndAbility()`: `EndAbilityEvent`, `OnAbilityEnded`, chain window): the client confirm timeout, the server's end RPC, the task heartbeat watchdog, `CancelAbilitiesWithTag`, an effect's `CancelAbilityOnActivation` / `CancelAbilityOnEnd` and `EndAbilityOn*Query`, `EndOtherAbilitiesQuery`, `BlockOtherAbilitiesQuery` | all of them call `CancelAbility()`: no `EndAbilityEvent`, no `OnAbilityEnded`, no chain window; the new `CancelAbilityEvent` (BlueprintNativeEvent) and the component's `OnAbilityCancelled` fire instead. New `CancelAbilitiesByTag` / `CancelAbilitiesByQuery`; `EndAbilitiesByTag` / `ByClass` / `ByQuery` keep the natural end | cleanup that must run on both paths (stop loops, hide cosmetics, release state) implements `CancelAbilityEvent` as well as `EndAbilityEvent`, or binds `OnAbilityCancelled` next to `OnAbilityEnded`. Code that relied on a chain window opening or an end event after an interruption calls `EndAbilitiesByTag` explicitly |
| `OnAbilityActivated` fired from `BeginAbility` with the ability still `PreExecution`; a listener's `CancelAbility()` was undone and left a live instance | fires from `PreBeginAbility` with the ability `Initialized` (`IsActive()` true); a listener's `CancelAbility()` refuses the activation (no cooldown, no `BeginAbilityEvent`; the cancel hooks fire) | listeners that checked `IsActive()` or the state adjust |
| `BeginAbility` overrides could assume the ability was alive after `Super` | a cancel inside `Super::BeginAbility()` (from `CancelConflictingAbilities`) leaves it `Ended` | an override that creates tasks, declares effects or commits a cost after `Super` adds `if (AbilityState == EAbilityState::Ended) { return; }` |
| activation never checked `AbilityCost` | `PreBeginAbility` refuses an unaffordable activation (`Stopped By Cost` at Verbose) with `CanAffordAbilityCost()`'s default `DeltaTime = 1`: a `Ticking` or `Periodic` cost is judged over one second. A refused activation consumes the press (no fallthrough to the next candidate). Cost attributes that are not GMC-bound log one Warning per ability class | a `PreExecuteCheckEvent` that returned `CanAffordAbilityCost()` may stay or go; size ticking costs for one second at activation; bind cost attributes |
| `CommitAbilityCost` did not declare its instance: a `Ticking` / `Persistent` cost survived `CancelAbility` | a non-`Instant` cost is declared and ends with the ability on every path; `RemoveAbilityCost` also forgets the declaration | drop hand-written removal in cancel overrides (harmless if kept) |
| an ability instance copied a hand-kept list of 12 properties from its class default object | `NewObject(..., bCopyTransientsFromClassDefaults = true)`: every reflected property, transient ones included, as the engine copies them for Blueprint classes | native abilities whose class defaults are changed at runtime now see those values on new instances (and `ApplyEffectOnEnd`, `RemoveEffectOnEnd`, `EndOtherAbilitiesQuery`, `BlockOtherAbilitiesQuery` and the rest reach instances, which the old list omitted) |
| `StartingEffects` were polled from the prediction tick | applied from the server's ancillary tick once the pawn has a controller | nothing, unless code read them inside the first prediction tick |
| `SetAttributeValueByTag` changed nothing and returned `true` | changes nothing, returns `false`, logs one Warning per component; deprecated | apply a `Set` / `SetReplace` modifier through an effect or `ApplyAbilityAttributeModifier` |
| `GetAbilityCostValues` read each modifier's raw value (non-`AMT_Value` sources 0 with an Error), last modifier per attribute won | with an owner: per attribute, the projected change (target − current for a `Set`); on a class default object: the raw `AMT_Value` amounts summed per attribute (other sources 0, no Error) | UI that displayed it shows sums |

### Tier 3: fixes that change behaviour

Each row was a bug. Nothing to do unless your project relied on the old behaviour or coded a workaround, which you can now remove.

**Abilities** (`Public/Ability/GMCAbility.h`):

| 1.4.0 | 1.4.1 | What to change |
|---|---|---|
| an activation refused in `PreBeginAbility` ran `ApplyEffectOnEnd` / `RemoveEffectOnEnd` | a refused ("dead-born") activation fires no cancel hooks and applies no end effects | nothing, unless content relied on end effects of refused presses |
| `EndAbility` / `CancelAbility` could both run on one instance | one entry latch: the first end wins, the other call is ignored, exactly one hook set fires | nothing |
| `End*By*` returned counts including unpurged `Ended` instances | counts and loops skip them | nothing |
| `EAbilityState::Running`, `Waiting` visible | hidden (`DEPRECATED: never assigned`) | Blueprint *Switch on EAbilityState* nodes may show orphan pins: delete them |
| `BlockOtherAbilitiesQuery` displayed as *Block Other Ability via Definition Query* | *End Other Abilities On Begin (Definition Query)* (it cancels; it never blocked); the effect queries display as *Cancel Abilities On Activation / On End Via Definition Query* | nothing: property names and asset data unchanged |

**Abilities: cost and instancing:**

| 1.4.0 | 1.4.1 | What to change |
|---|---|---|
| `CanAffordAbilityCost` tested each modifier alone (two −30 on 50 passed) and treated `Set` as an add; attribute-sourced values read 0 | judged per attribute over all modifiers from the current `Value`; `Set` / `SetReplace` absolute; attribute-sourced values and `Conditions` resolved through the owner | costs that passed by accident now refuse |

**Effects** (`Public/Effects/GMCAbilityEffect.h`, `Public/Components/GMCAbilityComponent.h`):

| 1.4.0 | 1.4.1 | What to change |
|---|---|---|
| the grace deferral of removals was disabled (`bHasGracePeriod = false`); `ClientGraceTime` / `DefaultClientGraceTime` changed nothing; the `bUniqueByEffectTag` replace path was unreachable | restored: a networked removal of a `Ticking` / `Periodic` effect arms `EndAtActionTimer = ActionTimer + grace` on both sides and the effect keeps ticking until then; a same-tag apply in that window replaces the old instance. `DefaultClientGraceTime` minimum 0; `0` = no deferral (opt-out); a per-effect `ClientGraceTime > 0` still defers | to keep 1.4.0's instant removal set `DefaultClientGraceTime = 0` (Project Settings → GMC Ability System → Network Timing); expect removed drains to apply for up to 0.5 s more otherwise |
| the id was assigned and the instance registered after `StartEffect`: `OnEffectApplied` / `StartEffectEvent` saw `EffectID == 0` and `GetEffectById` failed | id and registration first; listeners see the final id and can query the instance | lookups inside listeners now work; remove workarounds |
| `Instant` effects ran `EndEffect` before `StartEffectEvent` and ended in state `Started` | `StartEffectEvent` then `EndEffect`; final state `Ended`; no `RPCClientEndEffect` for them; a completed `Instant` effect skips the bound id list | code that read the state of a finished `Instant` effect expects `Ended` |
| an apply refused by `ApplicationMustHaveTags` / `MustNotHaveTags` / `ActivationQuery` returned `true` and a dead registered instance | returns `false` / `nullptr`, registers nothing, fires neither `OnEffectApplied` nor `OnEffectRemoved` | callers check the return instead of `bCompleted` |
| `MustMaintainQuery` was checked from the first tick | also checked at start: an effect that would end on its first tick is refused | nothing, unless content relied on the one-tick flicker |
| during `Delay`, `TickEvent` ran and `CurrentDuration` went below 0 | nothing ticks before the start | tick-driven logic waits for the start |
| `GrantedAbilities` not refcounted: the first of two granting effects to end removed the grant | a grant another live effect still makes survives (`bPreserveGrantedTagsIfMultiple`); a direct `GrantAbilityByTag` grant is not protected | nothing |
| a buffered `PredictedQueued` apply returned `true` with id `-1`; a unique-tag rejection inside a move returned `true` | the buffered apply returns the id it reserved; a rejection returns `false` | keep the returned id |
| `ServerInstantAttribute` checked only `EffectType`, `GrantedTags`, `GrantedAbilities` (cancel and chain fields then ran server-only); fallback with `ensure` + Warning | also requires no `Delay`, no `CancelAbilityOn*`, `EndAbilityOn*Query`, `ApplyEffectOnEnd`, `RemoveEffectOnEnd`, in the class and the inline data; otherwise an Error and the `ServerAuth` path | effects that relied on the server-only side effects now travel `ServerAuth` (one round trip later) |
| `RemoveSynchronizedTag` matched hierarchically (removed child tags' effects) | exact match | nothing, unless content relied on removing children |
| effect-id range overflow (about 83 days of move time, or a negative clock) was `Fatal` | wraps inside the range with one Error per component (`[EffectID] <Generator>: ... ids wrap`); ids stay unique among live and reserved effects | nothing; ids can repeat across a very long session |
| a predicted removal outside a move, and other data errors, hit `ensure` / `checkNoEntry` | an Error log and a skip | log watchers catch the Errors |

**Attributes and modifiers** (`Public/Attributes/`):

| 1.4.0 | 1.4.1 | What to change |
|---|---|---|
| `bStartFull` won over `SetAttributeInitialValue`; the hook ran before dependent rows resolved | rows resolve, the hook runs once per attribute, then every row settles again; the hook wins over `bStartFull`, never over the clamp (Error, clamped) | overrides that were silently lost now apply |
| `GetAttributeInitialValueByTag` returned the row's `DefaultValue` | the value actually applied; `-1` and one Warning per tag for an unknown tag | callers that wanted the asset value read the data asset |
| duplicate attribute tags resolved silently; an active `[0, 0]` clamp was silent | Error naming the asset(s); the second declaration ignored; `[0, 0]` reported at init (a deliberate zero pin too) | fix the rows |
| `AddScaledBetween` clamped to the literal `X`..`Y` even with attribute bounds | the alpha is clamped, so the result stays between the resolved bounds | values with attribute bounds change |
| `AddPercentageMaxClamp` / `MinClamp` read the literal bound with the flag off | contribute 0 (one Warning per attribute) while the bound's flag is off | set the flag or use another op |
| `AddPercentageOfAttributeRawValue` ignored `ValueAsAttribute` | uses `ValueAsAttribute`'s `RawValue` when set, else the target's own | modifiers that set the field (the editor shows it) now read it |
| `AMT_Custom` with a null class or a stale source effect hit `checkNoEntry` | an Error and 0 | nothing |

**Tasks** (`Public/Ability/Tasks/`):

| 1.4.0 | 1.4.1 | What to change |
|---|---|---|
| `WaitForInputKeyPress`'s activation poll completed when the key was **not** pressed | completes when it is pressed | abilities that worked around the inversion remove the workaround |
| `WaitForInputKeyPressParameterized` gated and polled the ability's `AbilityInputAction` | its own `InputAction` throughout | nothing, unless content relied on the wrong action |
| press tasks polled live input during replays; a dedicated server for a remote pawn queued payloads it never sent | live polls only where `DrivesPawnLocally()` and never during a replay; such a server only waits | nothing |
| only `SetTargetDataVector3` checked the payload's struct; the dispatcher read any struct | every `SetTargetData*` and both dispatch sites compare the struct exactly; a mismatch is dropped with one Error | a custom task that sends a derived struct through a built-in `SetTargetData*` sends the exact type |
| `WaitForRotateYawTowardsDirection` measured world time | move time (`ActionTimer`) | `Duration` values change slightly |
| `WaitForGameplayTagChange` never unbound and could complete twice | unbinds in `OnDestroy`, completes once, broadcasts after `EndTask` | nothing |
| the task heartbeat watchdog logged to `LogGMCReplication` and `LogTemp` | `LogGMCAbilitySystem` (`[TaskHeartbeat] Timeout: cancelling ability ...`) | log filters follow |

**Diagnostics:**

| 1.4.0 | 1.4.1 | What to change |
|---|---|---|
| `BL.GMAS.DumpAttrBindMap`, `[BLMoveEnqueue]` lines | `GMAS.DumpAttrBindMap`, `[AttrBindMap]` lines; the old command still works with a deprecation Warning until 1.5 | rename in scripts |
| `LogTemp` mirrors and GMC-category lines from GMAS; opt-in traces (`[ProcessOp]`, `[ServerOpAccept]`, `[ServerOpDrop]`, `[ApplyTrace]`) at Warning; one ungated `[ServerOpDrop]` Error; `GMAS.ApplyTraceFilter` with a leftover default | everything on `LogGMCAbilitySystem`; traces at Log behind `GMAS.LogApplyTrace`; the filter empty by default | log watchers keyed on `LogTemp` or on Warning-level traces adjust |
| `[ApplyAbilityEffect]` / `[GetNextAvailable*EffectID]` id errors | `[EffectID] <Generator>:` (`Predicted`, `ServerAuth`, `ClientAuth`); new `[OperationLost]` Error for a client operation the server never cached | log filters follow |
| `[GMAS-LIVE-CHECK] Module loaded — build tag ...` at Warning | `GMAS <version> loaded` at Log | nothing |

**Deprecated in 1.4.1, removed in 1.5** (each logs one Warning per component when used, or carries Blueprint deprecation metadata):

| Deprecated | Replacement |
|---|---|
| `ExecuteSyncedEvent` (does nothing), `OnSyncedEvent` (never broadcast), the types in `Utility/GMASSyncedEvent.h` (`UGMASSyncedEvent`, `FGMASSyncedEventContainer`, `FGMASSyncedEventData_AddImpulse`) | `FireCustomEvent` / `OnCustomEvent`; `SetActorLocation` for teleports |
| `SetAttributeValueByTag` | a `Set` / `SetReplace` modifier through an effect, or `ApplyAbilityAttributeModifier` |
| `OnPreAttributeChanged` (never broadcast; not restored: it would run Blueprint code that edits modifiers inside every predicted apply and replay) and its payload `UGMCAttributeModifierContainer` | a custom calculator (`UGMCAttributeModifierCustom_Base`) for pre-change logic, `OnAttributeChanged` after |
| `GetQueuedAbilityCount` (always 0) | none |
| `GetEffectFromHandle_BP`, `GetActiveEffectByHandle` | `GetEffectById` (handles are effect ids) |
| `EGMCAbilityEffectQueueType::ServerAuthMove` (an alias of `ServerAuth`) | `ServerAuth` |
| `UGMCAbilityTaskBase::IsClientOrRemoteListenServerPawn()` | `DrivesPawnLocally()` |
| `BL.GMAS.DumpAttrBindMap` | `GMAS.DumpAttrBindMap` |

`FGMCAttributeModifier::MetaTags` is user metadata (GMAS carries it and never reads it) and is not deprecated. Earlier deprecations (`RemoveActiveAbilityEffectByHandle`, `RemoveEffectByHandle`, `RemoveEffectByTag`, `RemoveEffectById`) are unchanged.

**Procedure for 1.4 → 1.4.1:** move the submodule to `v1.4.1` and rebuild client and server together (tier 1: the task payload changed); fix any compile errors with the tier 1 tables and treat each new deprecation warning as a work item. Then walk tier 2: every `EndAbilityEvent` that also cleans up after an interruption (add `CancelAbilityEvent`), every `BeginAbility` override with work after `Super`, every cost that relied on the missing gate, every `SetAttributeValueByTag` caller that checked the return. Skim tier 3 for workarounds you can delete, and for ticking effects whose removal timing matters (or set `DefaultClientGraceTime = 0`). Run the `GMAS.*` specs (all pass on 1.4.1) and a networked PIE session (`gmas:gmas-testing`).

## 1.4 → 1.5

| Before | 1.5 | Do |
|---|---|---|
| `RPCConfirmAbilityActivation` and `RPCClientEndAbility` | replaced by `ClientAbilitySync` / `ServerAbilitySync` (private; one message struct for answers, ends and digests). Only a component subclass that overrode the old RPCs breaks | delete the overrides; use `OnAbilityEnded` / `OnAbilityCancelled` |
| `TryActivateAbility(Class, ...)` | gained trailing defaulted parameters (`SourceOperationID`, `SourceCandidateIndex`, `bClientAuthorized`) | nothing for existing callers; an override or function pointer needs the new signature |
| a server natural end only removed the client copy; a server refusal left it until the confirm timeout | a natural end runs `EndAbility` on the client (end event, `OnAbilityEnded`, chain window); a refusal is answered `Rejected` and the client copy is cancelled after one round trip | cosmetics and chain windows now fire on the client too: guard them like any live-move work; expect `CancelAbilityEvent` early, not 2 s later |
| an orphan (an instance on one side only) lived on | digest and orphan ends: both sides end it (`[AbilityReconcile]`, Warning) | rework a design that relied on an unmatched instance surviving |
| client-authorized abilities timed out on the client; confirm timeouts read `ActionTimer` | client-authorized abilities are exempt; timeouts read a local confirm clock and the `[AbilityCut]` line ends `(no ability answer received)` | none |
| none | `AbilityReconcileMinAge` 1 s, `AbilityDigestInterval` 1 s, `AbilityAnswerHoldTime` 5 s in `UGMASNetworkTimingSettings` (`Public/Settings/GMASNetworkTimingSettings.h`) | tune only with a reason; fault cvars `GMAS.Debug.*` in `gmas:gmas-debug` |

**Procedure for 1.4 → 1.5** (the contract: `gmas:gmas-rules`, `## Ability lifetime and convergence`): move the submodule, rebuild client and server together, fix compile errors from the table, then review every ability armed once at spawn and left running: convergence ends it on a mismatch and nothing revives it, so scope it to a press or an action, or re-activate it from game logic (`gmas:gmas-ability`). Run the `GMAS.*` specs and a networked PIE session: no `[AbilityReconcile]` in steady play.

## Later releases

Nothing after 1.5 exists yet. Maintainer, when a release `vX.Y.0` is tagged (`gmas:gmas-maintain`):

- Add a `## 1.5 → X.Y` section directly after `## 1.4 → 1.5`, same groups; this file is near 300 lines, so first move the `## 1.3 → 1.4` tables and its procedure to `references/1.3-to-1.4.md` and leave a one-line pointer.
- Build it from `gh release view vX.Y.0 --json body -q .body` (the "Breaking changes" list) and `git diff v1.5.0 vX.Y.0 -- Source/GMCAbilitySystem/Public`; for each changed header list every removed or re-signed public, protected or virtual declaration with its replacement. Verify each pair against `git show vX.Y.0:<path>`: release notes drift (1.4's listed a still-present method and a non-existent field).
- Hunt the silent changes: defaults that flipped (PowerShell: `git diff v1.5.0 vX.Y.0 -- Source/GMCAbilitySystem/Public | Select-String -Pattern '^[-+].*= *(true|false|[0-9.]+f?);'`; bash: `git diff v1.5.0 vX.Y.0 -- Source/GMCAbilitySystem/Public | grep -E '^[-+].*= *(true|false|[0-9.]+f?);'`), enum values hidden or aliased, semantics changed under an unchanged name, settings that moved to `UGMASNetworkTimingSettings`.
- Update the `(1.4+)` tags across the other skills where a fact changed again, the hook's generation probe if the marker header moves, and the "Known failing set" in `gmas:gmas-testing`.

## Checklist

- The installed generation and the target release are known before any edit; the move is tag to tag, not to a moving `dev` unless that is the intent.
- Every `UGMCAttributesData` row has a deliberate clamp (bounds set or flags off); attributes read non-zero on server and client in PIE.
- No reference to `GMASBoundQueue.h`, `TGMASBoundQueue*`, `PreRemoteMoveExecution`, `CreateEffectOperation` or the legacy `ApplyAbilityEffect(Class, Data, bool)` remains in C++, and no Blueprint graph calls "Execute Synced Event", "Pre Remote Move Execution" or "Apply Ability Effect (Legacy)"; every apply names a queue type and keeps `OutEffectId` or an `EffectTag`; `ServerAuthMove` is written `ServerAuth`.
- `bActivateOnMovementTick` is explicit on once-only abilities and consistent per input tag; `bPreserveGrantedTagsIfMultiple` was decided for every effect that shares a granted tag.
- Tasks overriding `OnDestroy` call `Super`; effect subclasses with their own `EndEffect` say `override`; FX calls pass `UserParams`.
- The build has no GMAS deprecation warnings you did not consciously keep; a 5.4 project knows about the unguarded include.
- `GMAS.*` specs: only the known failing set fails; the project's tests are green and drive the new API.
- Networked PIE: no `[AbilityCut]`, no `Not Confirmed By Server`, no replay bursts, server operations on bots land next tick.
- Pointer, data and code are committed together; no local patch lives inside the plugin; `gmas:gmas-rules` was re-read.
- 1.4 → 1.4.1: client and server built from the same tree; interruption cleanup also in `CancelAbilityEvent`; `BeginAbility` overrides return when `Ended` after `Super`; costs sized for the activation gate; no caller relies on `SetAttributeValueByTag` returning `true`; the grace deferral kept or `DefaultClientGraceTime = 0` chosen; deprecation warnings scheduled before 1.5.
