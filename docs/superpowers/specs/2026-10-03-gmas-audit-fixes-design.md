# GMAS 1.4.1 — source audit fixes — design

**Status:** approved 2026-10-03, not yet implemented.
**Scope:** one pull request against `dev` that fixes the defects found while verifying the Claude Code plugin's skills against the GMAS source (the audit table is `docs/audits/2026-10-03-source-audit.md`; items are referenced below as A#n).

## 1. Context

Writing the `gmas` plugin meant reading every public header and most of the component against three trees (`v1.3.0`, `v1.4.0`, `dev`). That surfaced 133 findings: logic defects (an accidental "temporary" switch that disables the deferred-end machinery, a crash when the movement-component pointer is unset, activation that never checks cost, abnormal ends that open chain windows, client payloads cast without a type check on the server), public APIs whose bodies are commented out, defaults that changed silently between 1.3 and 1.4, and hygiene (downstream project names in public source, `LogTemp` mirrors, stale comments, test-only classes compiled into Shipping). The 1.4.0 release notes were amended as the findings came in; this design fixes the source.

Two refactor waves explain most of the dead code: the 2025 attribute refactor (modifiers became the only write path) and the 2025 BoundQueueV2 rewrite (the V1 queue, synced events and the replicated effect-data array were removed and only partly replaced). Each dead item was traced to the commit that disabled it before a disposition was chosen.

## 2. Goals and non-goals

Goals:

1. Fix every logic defect in the audit (group A) with a spec where the headless seams allow, otherwise with a networked play-in-editor check recorded in the pull request.
2. Give every dead or inert public API a deliberate disposition: restore it if the history shows it was meant to come back, deprecate it (with a message that names the replacement and a one-shot warning when called) if a replacement exists, remove it if it has no external surface.
3. Remove the mechanical hygiene debt (group C) that is cheap and safe.
4. Make the test harness able to drive abilities and tasks headless, so the fixes above can be covered.
5. Call out every behavior change, rename and removal in the pull request under **Breaking changes**, with old → new → migration.

Non-goals:

- Writing a spec for every test gap the audit noted; only the specs the fixes need (and the shared helpers that make them possible).
- A 64-bit effect-id scheme; the overflow becomes an error with wrap, not a redesign.
- Changing the bound-queue protocol or the attribute layering model.
- Compile CI (unchanged policy); the gates run on the maintainer's machine.

## 3. Branch, pull request, version

- Branch `fix/audit-1.4.1` from `dev` (`fe0b995`). One pull request to `dev`. The maintainer reviews and merges; nothing is auto-approved.
- One commit per group below (plus a "harness" commit first and a "docs" commit last), each leaving the tree buildable and the specs green or improved.
- `VersionName` stays `1.4`; the `1.4.1` tag and release happen at the next promotion (`docs/BRANCHING.md`). The release notes are drafted in the pull request description so promotion can reuse them.
- Nothing from a downstream project (names, paths, assets) enters the branch; the existing leak grep applies to every commit.

## 4. Policies

**Dead and inert APIs.** Disposition rule, applied after reading the disabling commit:
- *Restore* when the surrounding design still depends on it and the disabling commit says "temporary" or "re-add later" **and** restoring is safe for prediction (runs where it ran before, or outside the prediction tick).
- *Deprecate* when a replacement exists or the function is reachable from Blueprint: `UE_DEPRECATED(5.8, "GMAS 1.4.1: … use X")` on C++ (where the symbol is not a `UFUNCTION`), `meta=(DeprecatedFunction, DeprecationMessage="…")` on `UFUNCTION`s, `UMETA(Hidden, DisplayName="DEPRECATED: …")` on enum values, and a one-shot `Warning` under `LogGMCAbilitySystem` the first time an inert function is called. Deprecated items are removed in the next minor.
- *Remove* when nothing outside the module can reach it (private members, unused structs, commented declarations, test-only helpers).

**Behavior fixes.** Fixed straight, no compatibility flags, each listed under Breaking changes. The one exception is an **opt-out** for the restored grace-time deferral (a project may want instant visual removal).

**Logging.** Data errors (bad asset, null class, unknown tag) log an `Error` and continue; `check`/`checkNoEntry`/`Fatal` stay only for programmer invariants. Opt-in traces log at `Log`. GMAS never logs to `LogTemp` or to GMC's categories.

**Deprecation of console surface.** A renamed console command keeps the old name registered for one minor with a message pointing at the new one.

## 5. Work groups

Each table row: audit item → change → verification → **B** when it is a breaking change or behavior change for the pull request notes.

### 5.0 Harness (first commit)

| Item | Change | Verification |
|---|---|---|
| A#91, A#94, A#116 | Add `TickActiveAbilitiesForTest(float DeltaTime)` and `SetActionTimerForTest(double)` seams next to `TickActiveEffects`/`ProcessAttributes`; `GenPredictionTick` keeps overwriting `ActionTimer` from the movement component, but the seam lets a spec advance a controlled clock and tick abilities through the real `TickActiveAbilities`. | A spec activates a bundled test ability, advances the seam, observes `Tick` and task completion. |
| A#93 | `UGMCAbility`'s confirm-timeout check uses `IsAuthorityForGMASLogic()` so `bForceAuthorityForTest` reaches it; headless server-side abilities no longer die by `[AbilityCut]`. | The same spec runs past two seconds of `ActionTimer` without an `[AbilityCut]` error. |
| A#22, A#115 | A shared test helper (`GMAS_TestHelpers.h` under `Private/Tests`): `MakeAttr` with an explicit open clamp or a real `Max`, and one `ActionTimer` seeding convention (`-1.0`) with the reason in a comment; every spec uses it. | The ~150 attribute specs pass; no spec defines its own `MakeAttr`. |
| A#113, A#114 | `UGMAS_TestBoundAttrAbility` comment corrected and its tag requested with `ErrorIfNotFound = true`; the three layer-3 helper abilities either gain a spec in this pull request (they are used by the cost, cancel and task specs below) or are deleted. | Build; the new specs reference them. |
| A#132, A#17 | Test stub classes and spec files compile only `#if WITH_AUTOMATION_WORKER` (and never in Shipping); `Private/Tests` removed from the public include paths; `GameplayDebugger` dependency only when `bBuildDeveloperTools`. | A Shipping-configuration build of a consumer compiles; the specs still run in the editor. **B** (consumers that included test headers) |

### 5.1 Attributes (A)

| Item | Change | Verification |
|---|---|---|
| A#15, A#39 | `InstantiateAttributes` logs an `Error` naming the asset and tag when a row resolves to an active `[0, 0]` clamp with both flags on. | Spec: such a row produces the error; a row with `bClampMax = false` does not. |
| A#25 | `AddScaledBetween`: clamp with the resolved (attribute-driven) bounds, not the literal `X`/`Y`. **B** | Spec with attribute-driven bounds. |
| A#36 | `AddPercentageMaxClamp`/`MinClamp`: respect `bClampMax`/`bClampMin` (unset bound → contribute 0 and log once). **B** | Spec. |
| A#26, A#41 | `AddPercentageOfAttributeRawValue`: use `ValueAsAttribute` when set (as the editor implies), fall back to the target's `RawValue`, never to `Value`. **B** | Spec. |
| A#27 | `AMT_Custom` with a null class or a stale source effect → `Error` + 0, matching the other value sources. | Spec with a null class. |
| A#32 | Duplicate attribute tags across data assets → `Error` naming both assets; `SetAttributeInitialValue` applied after clamp and `bStartFull` so an override is honored (or rejected with an `Error` when outside the clamp). **B** | Spec. |
| A#31 | `GetAttributeInitialValueByTag` returns the initial value actually applied (after `bStartFull`/overrides); unknown tags log a `Warning` once. **B** | Spec. |
| A#38, A#40 | Error strings name the function that logs them; "Orphelin" → "Orphan"; `FAttributeClamp` pointer `operator==` null-safe. | Build. |
| A#33 | Documented in the header (no code change): permanent writes are restored only through the bound `RawValue`; an ended `Instant` effect is not re-applied on replay. | — |
| A#8 | C4305 literal fixed. | Build warning gone. |

### 5.2 Effects (A)

| Item | Change | Verification |
|---|---|---|
| A#1, A#46, A#59, A#64 | Restore the grace-time deferral: `bHasGracePeriod = EffectiveGraceTime > 0.f`; `DefaultClientGraceTime` minimum becomes `0` and `0` means "no deferral" (the opt-out); the self-skipping replace specs become assertions; the chain-window comment about re-grant is made true or corrected. **B** | The replace specs assert; a networked PIE session removes a Ticking effect from the ancillary tick on client and server and both end at the same `ActionTimer` with no correction; the same with `DefaultClientGraceTime = 0` ends instantly. |
| A#42, A#55 | Apply order: assign the id, register the instance, then `InitializeEffect`/`StartEffect` and broadcast; Instant effects fire `StartEffectEvent` before `EndEffect`; a completed effect is not reset to `Started`. **B** | Spec: delegates observe a non-zero id and the instance is queryable from inside `OnEffectApplied`; state after an Instant apply is `Ended`. |
| A#43 | An apply refused by application tags or `ActivationQuery` returns `false`/`nullptr`, is not registered, fires `OnEffectRemoved` only if it fired `OnEffectApplied`. **B** | Spec. |
| A#44 | `GetActiveEffectsByTag`: `IsValid(V) && (bMatchExact ? A : B)`. | Spec with a null entry and both match modes. |
| A#45 | `RemoveEffectsByQuery` snapshots ids before removing (like its siblings). | Spec where an end hook applies another effect. |
| A#49 | `RemoveGrantedAbilityByTag` refcounts grants (or re-grants from the surviving effects on removal), mirroring the preserve rule for tags. **B** | Spec: two effects grant one ability; ending one keeps it. |
| A#50 | No `TickEvent` and no `CurrentDuration` decrement during `Delay`; ongoing `MustHaveTags` checked only after start. **B** | Spec. |
| A#51 | A buffered `PredictedQueued` apply returns the reserved id (or documents `-1` and returns `false` when no id exists yet). **B** if the return changes | Spec. |
| A#57 | `ServerInstantAttribute` guard also rejects effects with `CancelAbilityOnActivation/OnEnd`, `ApplyEffectOnEnd`, `RemoveEffectOnEnd`, `EndOtherAbilitiesQuery`, `EndAbilitiesByQuery`; they fall back to `ServerAuth`. **B** | Spec. |
| A#56 | Completed Instant effects do not enter `BoundActiveEffectIDs`/`ProcessedEffectIDs` as Pending (or are cleared the same tick). | Spec counts bound ids after a cost apply. |
| A#58 | `RemoveSynchronizedTag` removes the exact synced-tag effect, not children. **B** | Spec. |
| A#121 | `EndEffect` virtual: documented in the header (intended override point). | — |

### 5.3 Abilities (A)

| Item | Change | Verification |
|---|---|---|
| A#60, A#61, A#73, A#85, A#88 | Every abnormal or external end calls `CancelAbility()`: the client confirm timeout, `RPCClientEndAbility`, the task heartbeat watchdog, `CancelAbilitiesWithTag`, effects' `CancelAbilityOnActivation`/`OnEnd`, `EndOtherAbilitiesQuery`/`EndAbilitiesByQuery`. `EndAbility()` remains the ability's own natural end. Log text matches the call. **B** | Spec: a cancelled ability fires no `EndAbilityEvent` and opens no chain window; the natural end still does. |
| A#5, A#14 | `PreBeginAbility` refuses (and logs at `Verbose`) when `CanAffordAbilityCost()` is false. **B** | Spec: unaffordable cost → no `BeginAbility`, attribute unchanged. |
| A#63 | `CommitAbilityCost` declares its instance so `CancelAbility` removes a `Ticking`/`Persistent` cost. **B** | Spec. |
| A#62 | `BlockOtherAbilitiesQuery` keeps its name (asset data) but its comment and display name say what it does ("End other abilities on begin (query)"); no new blocking semantics. | — |
| A#68 | `GetAbilityCostValues` sums modifiers per attribute and resolves attribute-sourced values through a transient effect data, or returns only `AMT_Value` entries and says so in the header. | Spec. |
| A#69 | `SetCooldownForAbility` with an empty `AbilityTag` logs an `Error` once per class. | Spec. |
| A#66, A#75 | The explicit CDO→instance copy block is deleted (UObject construction already copies every `UPROPERTY`); a spec sets one of the formerly omitted properties on the CDO and reads it on the instance. | Spec. |
| A#67 | `EAbilityState::Running`/`Waiting` → `UMETA(Hidden, DisplayName="DEPRECATED…")`. | Build. |
| A#72, A#74 | Stale comment fixed; `CooldownTime` gets an in-class initializer. | Build. |
| A#12 | Documented in the header: a server-sent activation batched with operations whose candidates all fail tag checks applies through the grace force; abilities should keep the first candidate passing. | — |

### 5.4 Tasks (A)

| Item | Change | Verification |
|---|---|---|
| A#80, A#89 | Every `ProgressTask` and the two dispatch sites check `GetScriptStruct()` before `Get<T>()`; a mismatched client payload logs an `Error` (with the sender) and is dropped. | Spec sends a wrong struct through the dispatcher; no assert, one error. |
| A#76 | `WaitForInputKeyPress`'s activation poll completes on *pressed* (`Magnitude != 0`); the Parameterized variant uses the same predicate; header comments corrected. **B** | Spec with a stub input value where the harness allows; otherwise PIE check noted. |
| A#77 | Parameterized task gates and polls `InputActionToWaitFor`; owner guard added; a missing input component completes/fails deterministically instead of hanging. **B** | Spec/PIE. |
| A#78 | Press and Parameterized skip the live poll on replay like Release. | Spec with `bForceReplayForTest`. |
| A#79 | "EndOnStart" is `Verbose`, not `Error`. | — |
| A#81, A#92 | `WaitForGameplayTagChange` stores its delegate handle, removes it in `OnDestroy`, latches completion, and broadcasts after `EndTask`. | Spec: a second tag change after completion does not fire. |
| A#82 | `WaitForRotateYawTowardsDirection` measures time with the ability's `ActionTimer`. **B** (timing now move-time) | Spec via the new seam. |
| A#83 | `bTickingTask = true` removed from the three tasks. | Build; WaitDelay spec. |
| A#84 | New `UGMCAbilityTaskBase::DrivesPawnLocally()` with the correct meaning; `IsClientOrRemoteListenServerPawn()` deprecated and forwards. | Build. |
| A#90 | On a dedicated server for a remote pawn, the input tasks do not queue into `QueuedTaskData`; the server twin waits for the client payload only. | Spec counts `QueuedTaskData` on a stub server. |
| A#86 | `EGMCAbilityTaskDataType::Heartbeat` removed. **B** (wire value shift between mismatched builds) | Build. |

### 5.5 Component and queue (A)

| Item | Change | Verification |
|---|---|---|
| A#4, A#13 | At bind, a null `GMCMovementComponent` resolves to the owner's `UGMC_MovementUtilityCmp` via `FindComponentByClass`; still null → `ensureMsgf` + `Error`, bind nothing. | Spec: component without the pointer binds after auto-resolve; a pawn without a movement component logs. |
| A#106, A#110, A#109 | Effect-id range overflow → `Error` and wrap within the range instead of `Fatal`; the four generators share one tag `[EffectID]`. | Spec forces `ActionTimer` near the range end. |
| A#101 | The "operation not found in payloads" `Error` is restored (gated so the replay fallback does not log). | Spec. |
| A#112 | `RemoveFilteredTagChangeDelegate` removes by handle across all containers. | Spec. |
| A#20 | `ApplyStartingEffects` runs once from the first ancillary tick on the server (or `BeginPlay` when the owner is ready), not polled in the prediction tick. **B** (timing) | Spec/PIE. |
| A#125 | The 1 s server-operation grace moves to `UGMASNetworkTimingSettings::ServerOperationGraceSeconds`. | Spec reads the setting. |
| A#52 | `ClientEffectApplicationTimeout` member removed (private). | Build. |
| A#107 | `CheckValidState` reports an undroppable id once, not every tick. | Spec. |
| A#117 | Every caller of `GetNextAvailableEffectID` handles `-1` (audit, then fix). | Build + spec. |

### 5.6 Dead and inert APIs (B)

| Item | Disposition | Detail |
|---|---|---|
| A#2, A#127 | Deprecate, then remove next minor | `ExecuteSyncedEvent` and `OnSyncedEvent` deprecated with the message "use FireCustomEvent / OnCustomEvent"; the function logs once and does nothing else; `Utility/GMASSyncedEvent.h` types stay for one minor. Spec: `FireCustomEvent` through the test seam broadcasts `OnCustomEvent`. **B** (deprecation) |
| A#3, A#37 | Deprecate (honest) | `SetAttributeValueByTag` returns `false`, logs once with the replacement (`Set`/`SetReplace` modifier through an effect or `ApplyAbilityAttributeModifier`); comments rewritten. **B** (return value) |
| A#23 | Deprecate | `OnPreAttributeChanged` deprecated (never broadcast since the attribute refactor); `UGMCAttributeModifierContainer` removed. |
| A#24 | Remove | `FAttribute::OnAttributeChanged` (never broadcast; shrinks the struct). **B** |
| A#65 | Deprecate | `GetQueuedAbilityCount` deprecated; logs once. |
| A#104, A#105 | Remove / add | `GetActiveEffectsDataString` and the debugger's *Active Effects Data* row removed; `UGMCAbilityEffect::ToString()` gains the client answer state (`Pending`/`Validated`/`Timeout`) so the *Active Effects* row shows it. |
| A#122, A#119, A#53, A#28, A#29 | Remove | `FEffectStatePrediction`; commented `Create*Operation` declarations; `CalculatePeriodicTicksBetween`; `ProcessCustomModifier` + `CustomModifiersInstances` (custom calculators are documented as stateless, run on the CDO); `MetaTags` → `DeprecatedProperty` (asset data) then removed next minor. **B** (removals) |
| A#123 | Deprecate / remove | `GetEffectFromHandle_BP` and `GetActiveEffectByHandle` deprecated in favour of `GetEffectById`; the `EffectHandles` registry, its helpers, seams and the handle spec removed. **B** |
| A#47, A#120 | Deprecate | `ServerAuthMove` → `UMETA(Hidden, DisplayName="DEPRECATED: use ServerAuth")`, one-shot log when applied; removed next minor. |
| A#84 | Deprecate | see 5.4. |
| A#9, A#103, A#131 | Remove | `[GMAS-LIVE-CHECK]` banner removed; module startup logs `VersionName` at `Log`. |

### 5.7 Hygiene (C)

| Item | Change |
|---|---|
| A#111, A#124, A#18, A#95 | Downstream project names removed from comments and identifiers; `BL.GMAS.DumpAttrBindMap` → `GMAS.DumpAttrBindMap` (old name kept one minor with a pointer), help text describes GMC's own sync-data dumps; `[BLMoveEnqueue]` → `[MoveEnqueue]`. **B** (console rename) |
| A#96, A#97, A#108 | `GMAS.ApplyTraceFilter` default empty; opt-in traces at `Log`; the ungated `[ServerOpDrop]` gated like its siblings. |
| A#100, A#102, A#99, A#10 | `LogTemp` mirrors and the bound-queue `LogTemp` line → `LogGMCAbilitySystem`; the watchdog logs under `LogGMCAbilitySystem`, text matches the call. |
| A#98 | `[ReplayBurst]` text says what it measures (frames with a replay inside a window), not "sustained validation divergence". |
| A#16 | `GrantAbilityByTag`, `GetGrantedAbilitiesByTag`, `FAbilityMapData` comments say "input tag (ability-map key)". |
| A#11, A#19, A#72, A#121, A#126 | Stale comments, the commented base class, the typo. |
| A#17 | Epic copyright template lines replaced with the plugin's own header line. |
| A#30 | `GMCModifierCustom_Exponent.h` moved to `Public/Attributes/PreDefinedCustomCalculator/`. **B** (include path) |
| A#118 | `Utility/GMASBoundQueueV2.h` include guarded like its siblings (5.4 builds again). |
| A#130 | `.gitattributes` adds `*.uplugin`, `*.cs`, `*.cpp`, `*.h` as LF text. |
| A#129 | Sync script: check that `origin` points at the canonical repository; parse `gh pr list` with `--jq`. |

### 5.8 Docs

- Header comments updated where behavior changed (grace, cancel semantics, cost check, apply order, `ServerInstantAttribute` guard).
- `Claude/gmas` skills updated for every changed fact (grace restored; cost checked; cancel semantics; deprecations; renamed console command; `DrivesPawnLocally`); plugin `version` bumped to `1.4.1`.
- The audit table gains a status column (fixed / deprecated / deferred) in the docs commit.

## 6. Verification

- **Specs.** Baseline failing set recorded before the first commit (expected: the ~150 attribute specs). After the harness commit the attribute specs pass; every later commit keeps `GMAS.*` green and adds the specs its table names.
- **Consumer build.** Each group builds in a downstream project (editor target) and that project's automation suite stays green. A Shipping-configuration compile of the consumer proves the test-class guards.
- **Networked check.** A dedicated-server play-in-editor session (the `gmas-testing` skeleton or the consumer's own harness) exercises: grace deferral restored and opted out; a cancelled ability opening no chain window; an unaffordable activation refused on both machines; a mismatched task payload dropped with one error.
- **Leak grep** on every commit and on the pull request diff.
- **Plugin check.** `bash Claude/gmas/scripts/check.sh` green after the skills update.

## 7. Pull request description

Sections, in order: Summary (one paragraph per group); **Breaking changes** (every **B** row: what changed, old behavior, new behavior, migration); **Deprecations** (symbol, replacement, removal release); **Removed** (symbols with no external surface); **Behavior worth knowing** (non-breaking visible changes: new error logs, renamed tags); **Verification** (spec counts before/after, consumer build, PIE checks); **Left for later** (test gaps not covered, the 64-bit id question, downstream callers of deprecated APIs). The same text seeds the 1.4.1 release notes.

## 8. Risks

- Restoring the grace deferral runs the suspend/revive path in production for the first time; the PIE check and the opt-out are the mitigation.
- Cancel semantics and the cost check change what existing content does; both are visible in the first play session and are the headline breaking changes.
- Removing `FAttribute::OnAttributeChanged` changes the struct; the bound layout is derived at bind time, so no serialized data is affected, but the change is called out.
