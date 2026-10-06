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

A 1.3 tree (bound queue V1) moves to 1.4 first: the tables (bound queue, effects, abilities and the component, attributes, dependencies and includes), the silent behaviour changes (clamps pin at 0, `bActivateOnMovementTick` default `true`, `ServerConfirmTimeout` 2 s) and the eight-step procedure are in [references/1.3-to-1.4.md](references/1.3-to-1.4.md). Then continue with the sections below.

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

Limits of the convergence to know before the move:

- **Only activations from an operation are covered.** `QueueAbility` (the client's press, or a server-queued activation for a remote player) creates instances both sides track. A direct `TryActivateAbility` / `TryActivateAbilitiesByInputTag` call on both sides is not covered: the server's instance is server-only (never answered, never ended by sync), and the client's copy times out after `ServerConfirmTimeout` (2 s, `[AbilityCut]`) and is not ended by the server's end. Activate through operations (`QueueAbility`) whenever the owning client must hold a twin.
- **A reconnect is detected from the second snapshot request on one component.** If the first connection's snapshot request never reached the server, the next connection's request counts as the first: no reset runs, and the old connection's instances are only cleaned up by the digest (orphan ends after `AbilityReconcileMinAge` + `AbilityDigestInterval`).

**Procedure for 1.4 → 1.5** (the contract: `gmas:gmas-rules`, `## Ability lifetime and convergence`): move the submodule, rebuild client and server together, fix compile errors from the table, then review every ability armed once at spawn and left running: convergence ends it on a mismatch and nothing revives it, so scope it to a press or an action, or re-activate it from game logic (`gmas:gmas-ability`). Run the `GMAS.*` specs and a networked PIE session: no `[AbilityReconcile]` in steady play.

## Later releases

Nothing after 1.5 exists yet. Maintainer, when a release `vX.Y.0` is tagged (`gmas:gmas-maintain`):

- Add a `## 1.5 → X.Y` section directly after `## 1.4 → 1.5`, same groups; keep this file under 300 lines by moving the oldest section's tables to `references/` (as `references/1.3-to-1.4.md` was) and leaving a pointer.
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
