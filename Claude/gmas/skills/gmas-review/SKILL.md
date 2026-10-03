---
name: gmas-review
description: Use when reviewing a change that touches GMC or GMAS code, before merging or after a coding agent implements it.
---

> Behavior below was verified against GMC 2.3.x and GMAS 1.4. Confirm signatures in this project's own headers (paths are relative to each plugin's root, e.g. `Source/GMCCore/Public/Components/GMCReplicationComponent.h`); GMC's source is not part of this plugin.

This skill is a review procedure, not a reference. The rules live in `gmas:gmas-rules` (where state lives, binding order, queue types, server operations), `gmas:gmc-prediction` (the move cycle, replays, combined moves) and the authoring skills (`gmas:gmas-ability`, `gmas:gmas-effect`, `gmas:gmas-attribute`, `gmas:gmas-task`); this skill says what to verify, in which order, and how to report it. Inputs: the request (task text, plan or issue), the whole diff (`git diff --stat` first, then every hunk, new and deleted files included), the tests with their run output, and the implementer's reported deviations. Read the request before the diff, and review the code rather than its description: when a commit message says "guarded against replay", find the guard. GMAS paths below are relative to `Source/GMCAbilitySystem/`, GMC paths to the GMC plugin root.

## Two passes

**Pass 1, spec compliance.** Does the change do what was asked, all of it and nothing else?

- Every requested item is present: list them from the request and tick each against a hunk. An item delivered differently from the request is either a reported deviation or a finding, never silently accepted.
- Nothing extra: no unrequested features, unrelated refactors or renames, reformatting of untouched lines, or stray files (scratch, logs, generated content the project's generator owns, editor-saved assets the request did not name).
- Tests exist with the names and assertions the request stated, in the module it named; a requested test that was "deferred" is a missing item.
- Each reported deviation is named, has a reason, and is acceptable against the request; an unreported deviation you find is a finding.
- Documentation the request named (README, plan status, provenance notes) is updated.

**Pass 2, quality.** Correctness, networking, hygiene and tests, with the checklists below; each finding ranked Critical, Important or Minor (definitions under *Report format*).

Never start pass 2 while pass 1 has open issues: reviewing the quality of code that may be rewritten to meet the spec is wasted, and it tempts the author to fix the quality items and leave the spec gap. When spec fails, the Quality section of the report reads "not started (spec open)".

## Networking checklist

Trace every hunk that runs inside a GMC move or touches replicated state on three machines: the owning client, the server (dedicated, and a listen host whose own pawn is never predicted or corrected) and a simulated proxy. The guards and roles are in `gmas:gmc-prediction`; the GMAS placement rules in `gmas:gmas-rules`.

- **Authority.** Outcomes (damage, hits, kills, pickups, score, server operations) are decided only where `HasAuthority()` holds (`AActor`, or the ASC's inline role check in `Public/Components/GMCAbilityComponent.h`); a client branch predicts cosmetics and bound state, never results. Role branches use GMC's queries (`IsAutonomousProxy()`, `IsSimulatedProxy()`, `IsLocallyControlledServerPawn()`, `IsRemotelyControlledServerPawn()` in `Source/GMCCore/Public/Components/GMCReplicationComponent.h`), and both sides of every branch were read: the server branch assumes no local player or viewport (dedicated server), the client branch no authority, and whatever one side queues the other side drains.
- **Replay guards.** Every one-shot inside predicted code (spawn, FX, sound, RPC, score, damage) runs only on a live move: `!IsReplayingForGMASLogic() && !IsSimulatedMove()` on the owner (1.4+; `CL_IsReplaying()` on the movement component before), `!IsSimulatedMove()` on the server (`IsSimulatedMove()` in `Source/GMCCore/Public/Components/GMCMovementUtilityComponent.h`). A guard on one path and not its twin (the server half, an effect's `EndEffectEvent`, a task's `Completed` handler) is a finding.
- **Combined moves.** A one-shot raised from bound state (a cooldown reaching zero, a queued teleport, a shot) is followed by `CL_DoNotCombineNextMove()` or driven by an input bound `CombineIfUnchanged`; otherwise the next combined re-run of the same move fires it again.
- **Simulated proxies.** Nothing gameplay-relevant in `GenSimulationTick`; proxy cosmetics come from `GenSimulationTick`, from bound values with a `SimulationMode` other than `None`, or from change delegates. Data a proxy needs at spawn also replicates as an ordinary initial-only property (`PostSpawnSmoothingPause`).
- **Pure predicted logic.** `GenPredictionTick`, ability `TickEvent`, effect hooks and task `Tick` read only bound state, `ActionTimer`, the move's `DeltaTime` and `GetMoveTimestamp()`. No `GetWorld()->GetTimeSeconds()`, `FPlatformTime`, `FDateTime`, frame delta, timers, random numbers without a bound seed, anything that differs per machine (cursor, viewport, local player, frame rate, editor-only state), other pawns' displayed transforms, or unbound members written in one move and read in the next. Server-only bookkeeping is not polled from the prediction tick.
- **Bound vs replicated.** Every new `UPROPERTY`, attribute row and member has exactly one home from the "where does this state live" table in `gmas:gmas-rules`. Grep the diff for `DOREPLIFETIME`, `Replicated` and `bGMCBound = false`: none of them is read by predicted logic. Grep for new `Bind*` calls: none of them is read only by the HUD or cosmetics.
- **Binding.** `BindReplicationData_Implementation` calls `Super` first, binds the game's values in a fixed order with no condition around any bind, sets the ASC's `GMCMovementComponent` (nothing else sets it, and the bind dereferences it) and ends with the ASC's `BindReplicationData()`. No `Bind*` call anywhere else; the same bindings on every machine.
- **Clamps.** Every new or edited attribute row sets `Clamp.Max` or `MaxAttributeTag`, or unticks `bClampMax` (`Public/Attributes/GMCAttributeClamp.h`); the default clamp pins the value at 0 (1.4+). Same for the floor.
- **Cooldowns.** A cooldown the prediction tick or the HUD reads is a bound float on the movement component; `CooldownTime` / `ActiveCooldowns` (`Public/Ability/GMCAbility.h`) only where nothing else inspects it, because replays do not rewind them, and only with an `AbilityTag` set (an empty tag never cools down).
- **Presentation on every machine.** Predicted cosmetics use the ASC helpers with `bIsClientPredicted = true` on the live move; server-decided cues use `false` from the server; receivers skip when they have authority or are the predicted owner (the helpers do this; a hand-written multicast must); `bDelayByGMCSmoothing` where the cue must land on a proxy's displayed position; montages through GMC's `PlayMontage_Blocking`, never a multicast. The table is in `gmas:gmas-ability`.
- **Server operations.** `ApplyAbilityEffect(..., ServerAuth)`, `AddImpulse`, `FireCustomEvent` and activations on server-controlled pawns are called on the **target's** ASC, on the server, and nothing at the call site assumes the operation applied inline: it lands in the target's next move (or next ancillary tick for AI, 1.4+). The first granted candidate passes its gates on both sides.
- **Impulses.** `AddImpulse(FVector Impulse, bool bVelChange)` on the target's ASC from the server; "set velocity" is `Desired − Current` with `bVelChange = true`; nobody writes another pawn's velocity directly. A pawn's own dash writes its velocity inside its own prediction tick on both sides.
- **Activation.** Input and AI call `QueueAbility` from outside the pawn's move (input handler, AI tick, timer), never from `GenPredictionTick` or an ability tick of the same pawn; `TryActivateAbility*` appears only in server-only code or tests.
- **Time.** Code that compares move timestamps or `GetTime()` across machines has exactly one `AGMC_WorldTimeReplicator` spawned (`Source/GMCCore/Public/Replication/WorldTime.h`).

## GMAS-specific checks

- **Queue type justified per call** (table in `gmas:gmas-effect`): `Predicted` / `PredictedQueued` only from logic both sides run, on bound attributes; `ServerAuth` for what one pawn does to another and for starting effects; `ServerInstantAttribute` (1.4+) only for `Instant` attribute-only effects on unbound attributes with no `GrantedTags`, `GrantedAbilities`, cancel or chain fields (those run server-only there). Removal uses the queue type the apply used.
- **`bUniqueByEffectTag`** (1.4+, `Public/Effects/GMCAbilityEffect.h`) wherever single-instance or replace semantics are intended, and the author knows the 1.4 tree disables the grace defer: a refresh is remove-then-apply, and nothing relies on `ClientGraceTime` or `DefaultClientGraceTime`.
- **Data before bind.** `AttributeDataAssets`, `AbilityMaps`, `StartingAbilities`, `StartingEffects` and `StartingTags` are filled in class defaults, the constructor or `PostInitializeComponents` before `Super`, from data every machine has; `GMCMovementComponent` is set before `BindReplicationData()`.
- **Tags registered.** Every new `Attribute.*`, `Ability.*`, `Input.*`, `State.*`, effect and event tag exists in the project's tag ini or as a native tag (`RequestGameplayTag` on a missing tag ensures at runtime). Prefixes follow the pickers: `Input.*` is the map key, the `QueueAbility` argument and what `GrantAbilityByTag` takes; `Ability.*` is the ability's own tag.
- **Queries and tag matches not over-broad.** `ActivationQuery`, `EndOtherAbilitiesQuery`, `BlockOtherAbilitiesQuery` (it ends matching abilities; it blocks nothing), `EndAbilityOn*Query`, `RemoveEffectsByQuery`, `CancelAbilitiesWithTag`, `CancelAbilityOn*`, `RemoveActiveAbilityEffectByTag` and `RemoveSynchronizedTag` match hierarchically or by definition container: each was checked against every tag it could match, not only the intended one. Gates (`ActivationRequiredTags`, `ActivationBlockedTags`) hold bound tags only.
- **Ability end paths.** Every abnormal end (interruption, refusal, timeout, stun, death, owner lost) calls `CancelAbility()`; `EndAbility()` is for an ability that completed, because it runs `EndAbilityEvent`, broadcasts `OnAbilityEnded` and opens the chain window. Effects the ability must take with it are applied with `HandlingAbility = this` or registered with `DeclareEffect`; a `Ticking` cost is removed on both end paths. `CancelAbilitiesWithTag` is understood as a natural end of the victim.
- **Cost.** `PreExecuteCheckEvent` returns `CanAffordAbilityCost()`: activation never checks cost by itself, and with the 1.4 clamp floor an unaffordable `CommitAbilityCost` clamps at 0 and the ability is free. The cost is committed once.
- **`bActivateOnMovementTick`** identical for every class on one input tag (`true` is the 1.4 default; explicit on older trees); `false` only for work that must happen exactly once, knowing that only activation and task-payload dispatch move to the ancillary tick while `Tick` still runs in prediction.
- **Derived tags.** `MatchTagToBool` every prediction tick for state derived from bound values; no `AddActiveTag` / `RemoveActiveTag` outside the move; server-set tags through `AddSynchronizedTag` (1.4+).
- **Inert or dead API absent.** `SetAttributeValueByTag` (its write is commented out), `ExecuteSyncedEvent` (validates and queues nothing on 1.4), `OnPreAttributeChanged` and `FAttribute::OnAttributeChanged` (never broadcast), `GetQueuedAbilityCount` (returns 0). A new public function whose body is commented out or returns a constant is Critical.
- **Tasks** (`gmas:gmas-task`). `ProgressTask` checks `TaskData.GetScriptStruct()` before `Get<T>()`: the payload is client input the server has not checked; the value is clamped or rejected with a rule both sides apply; `bTaskCompleted` is latched before any broadcast and every path returns early on it; tasks are created from `BeginAbilityEvent` or `Completed` handlers only, identically on both sides; live polls (input, camera) only where `IsClientOrRemoteListenServerPawn()` and not replaying, with the result sent as a payload; `Activate` and `AncillaryTick` call `Super`; nothing sets `bTickingTask`.
- **Delegate handles stored and removed.** `AddFilteredTagChangeDelegate` and `AddAttributeChangeDelegate` (`Public/Components/GMCAbilityComponent.h`) return an `FDelegateHandle` that is kept and passed to `RemoveFilteredTagChangeDelegate(Tags, Handle)` / `RemoveAttributeChangeDelegate(Handle)` in `OnDestroy` or `EndPlay`; dynamic delegates get `RemoveDynamic`. A handle declared and never assigned is a finding.
- **Change delegates are presentation.** `OnAttributeChanged`, `OnActiveTagsChanged`, `OnEffectApplied` / `OnEffectRemoved` drive HUD and cosmetics; a gameplay decision in a handler is a finding. `OnEffectApplied` handlers use the instance they are handed, never a lookup by id or tag (the id is still 0 there).
- **Granted abilities are not refcounted.** Two effects granting one ability tag lose it when the first ends; the design accounts for it.
- **Identity.** Team, archetype and loadout ride initial-only replication and seed bound state; `GetExternalModifierValue` (1.4+) returns the same value on the server and the owning client.

## UE hygiene

- `UPROPERTY()` on every `UObject` reference a `UObject` holds (`TObjectPtr` or raw); `TWeakObjectPtr` for references it does not own; no raw actor or component pointer cached across frames without one of the two.
- Includes: each file includes what it uses (no reliance on transitive includes), `.generated.h` last, forward declarations in headers where a pointer suffices, no include of another module's `Private/` headers; `Build.cs` lists every module whose headers the change includes.
- Const-correctness: getters and predicates `const`; struct and container parameters `const&`; locals that never change `const`.
- No shadowed variables: a local or lambda parameter named like a member, an outer local or a function parameter (MSVC C4456 / C4457 / C4458; errors in many projects).
- No loop whose body always returns or breaks on its first iteration (clang `-Wunreachable-code-loop-increment`, "loop will run at most once"); no unused anonymous-namespace or `static` functions and no unused locals (`-Wunused-function`, `-Wunused-variable`); `f` suffix on literals assigned to `float` (C4305).
- Every override says `override` and calls `Super` where the base does work: `BeginPlay`, `EndPlay`, `PostInitializeComponents`, the five GMC hooks, `Activate`, `AncillaryTick`, `OnDestroy`.
- Every new member has an in-class initialiser; `TEXT()` on string literals; `IsValid()` before using an actor that may be pending kill; `Cast` results checked; no `GetWorld()` in constructors.
- `check` / `checkNoEntry` only for programmer invariants; data errors (a missing class, an unknown tag, a bad payload) log and return. Return values tell the truth: no function returns `true` or a handle after doing nothing. Nothing is declared and never used (fields, delegates, enum values, functions).
- No container mutated while it is iterated (removals snapshot the keys first); a ternary mixed with `&&` / `||` is parenthesised.
- Logging: new lines go to the right category (never `LogTemp` in shipped code), at a level that matches the event (`Warning` only for something a developer must act on; opt-in traces at `Verbose`), and healthy paths add no `Warning`. No `// TEMP`, `// TODO` or test switch left in a shipped path; no commented-out code.
- Matches the surrounding file: naming, bracing, tabs or spaces, header and source placement under the module's `Public/` and `Private/` layout; Blueprint-exposed members carry the specifiers and categories their neighbours use.

## Tests

- **Pure logic has a spec.** Attribute math, effect timing, ability gates, task completion: a spec in the style of `Private/Tests/GMAS_EffectSpec.cpp` (stub movement component, ASC, real data rows), one case per rule, gate or end path, as the authoring skill's *Which test to write* describes. Setup that edits a class default object restores it in teardown.
- **Assertions prove the behavior.** `TestEqual` on `GetAttributeValueByTag`, `GetAttributeRawValue`, `HasActiveTag`, counts and states after the step, not only "does not crash" or "returned true"; a negative case for each gate; the expected value computed from the rule, not copied from a first run.
- **Anything networked has a PIE test.** Predicted, replayed, confirmed, forced, multicast, smoothed: none of it exists standalone. The test runs a dedicated server plus at least one client (`gmas:gmas-testing`), with network emulation when the claim is about latency, and asserts on both worlds. A standalone test offered for a networking claim is a spec finding (the requested proof is missing), not a quality one.
- **Waits are bounded.** Every latent wait has a deadline that fails the test with a message; no wait on a fixed frame count or a sleep; windows derive from the mechanism (`ServerConfirmTimeout` 2 s, the 1 s server-operation grace, the 6 s task watchdog, `PostSpawnSmoothingPause` plus a settle) with slack for a slow machine.
- **Worlds and actors are re-resolved every step.** A `UWorld*`, pawn or component cached across latent steps dangles when the session ends or the actor respawns: tests look them up each frame from the editor's world contexts, or hold `TWeakObjectPtr`. Cleanup runs even when a step fails and restores cvars, verbosity and class defaults.
- **Logs are part of the assertion.** A networked test fails on `[AbilityCut]`, Warning-level `[TaskDiag]`, `[BatchOp]`, `Not Confirmed By Server` or a correction inside the window it measures (`gmas:gmas-debug`), or declares them expected with `AddExpectedError` and a reason.
- **Run them.** A review that did not run the tests, or read their output, says so in the report.

## Repository hygiene

- Edits to a vendored plugin (GMC, GMAS, any third party) are documented where the project records provenance (its plugins README or equivalent), with what changed and why. A plugin used as a submodule has no local-only change inside it: the fix went upstream and the diff is a pointer bump to a pushed commit.
- No GMC code copied anywhere: not into project source, tests, docs, skills, comments or commit messages. GMC appears by API name, a one-line signature and the header path.
- Examples in anything shared (plugin skills, upstream pull requests, wiki pages) use neutral names and carry no project names, drive or user paths, asset names or secrets; commit messages are descriptive and carry the trailers the project requires.
- Text files keep the repository's line endings (`.gitattributes`); no `Saved/`, `Intermediate/`, `Binaries/`, `DerivedDataCache/`, logs, screenshots or scratch files; generated content is regenerated by its generator, not hand-edited.
- For a change to GMAS itself: a default that flips (as `bActivateOnMovementTick`, `bPreserveGrantedTagsIfMultiple` and the clamp semantics did between 1.3 and 1.4) is listed under breaking changes in the release notes; a new public API has a spec; a deprecated function carries `UE_DEPRECATED` and logs, instead of a commented-out body.

## Report format

Under 500 words. One finding per item, each with `file:line`, what, why (the rule, citing the skill rather than restating it) and the fix. Do not list what is fine beyond the one-line summaries.

```markdown
## Spec ❌   (or ✅ — all N requested items present; deviations accepted: …)
1. `Source/MyGame/Private/MyMovementCmp.cpp:88` — `DashCooldown` is a plain member; the request asked for a bound float — predicted logic reads it, so a replay cannot restore it (`gmas:gmas-rules`) — bind it in `BindReplicationData_Implementation`.
2. `Source/MyGameTests/Private/DashSpec.cpp` — requested test `MyGame.Dash.CostIsSpent` missing — the request names it — add the spec with the stated assertion.

## Quality ❌   (or ✅; or "not started (spec open)")
### Critical
1. `file:line` — what — why — fix
### Important
1. …
### Minor
1. …

Verdict: one line — merge; fix Critical then re-review; back to spec.
```

Severity: **Critical** ships a bug or a leak: a desync or visible replay, a crash, the server trusting client data, an unguarded one-shot, a wrong queue type, predicted state replicated, a GMC excerpt or a project name or local path in shared text. **Important** is wrong but contained: a missing guard on a cosmetic, a warning that becomes an error elsewhere, a test that proves nothing, an unbounded wait, a delegate stored and never removed. **Minor** is style, naming, comments, ordering. When unsure between two levels, pick the higher and say why.

## Checklist

- The request was read before the diff; every requested item is ticked against a hunk; extras and stray files are named.
- Pass 2 started only after pass 1 closed; otherwise the report says "not started (spec open)".
- Every hunk inside a move or touching replicated state was traced on the owning client, the server and a simulated proxy.
- Every finding has `file:line`, what, why with the skill it comes from, and a fix; findings are ranked by the definitions above.
- The tests were run or their output read; networking claims have a networked test, pure logic a spec.
- Vendored edits documented, no GMC code copied, shared text generic.
- The report is under 500 words and ends with a one-line verdict.
