# GMAS Claude Code plugin — design

**Status:** implemented 2026-10-03 (see `Claude/gmas`). Wording below was aligned with the shipped hook and skills; the implementation notes at the end list the deviations.
**Scope:** a public Claude Code plugin, shipped inside this repository, that teaches Claude how GMC and GMAS work and how to author, debug, review and test GMAS gameplay.

## 1. Context

GMAS is an ability system built on GMC (General Movement Component). Everything predicted in GMAS rides GMC's move cycle: bound values, replays, combined moves, smoothing, move timestamps. Most mistakes people (and coding agents) make with GMAS are GMC mistakes: state that should be bound is replicated, side effects fire on replayed moves, cooldowns live where the replay cannot rewind them, attributes are set directly instead of through effects.

Claude Code can load this knowledge on demand through plugin *skills*. This repository already holds the canonical GMAS source and docs, so the plugin lives here and ships with GMAS releases.

Two facts shape the design:

- **GMC is a paid dependency and its source never enters this repository.** In any project that uses GMAS, GMC is nearby (`Plugins/GMC` or an engine plugin). The plugin must know that, point Claude at the project's own GMC headers for exact signatures, and never carry GMC code itself.
- **This repository stays generic.** No downstream project names, paths or game features in the plugin, its docs or its history.

## 2. Goals and non-goals

Goals:

1. A user runs two commands and Claude Code sessions in their GMC/GMAS project gain: automatic detection of GMC and GMAS (paths, versions, generation), and twelve focused skills covering GMC prediction, GMAS rules, setup, authoring (abilities, effects, attributes, tasks), debugging, review, testing, upgrading and maintaining this repository.
2. Skills capture behavior that headers do not reveal (what replays rewind and what they do not, where a server operation is applied, why an attribute reads 0) and cite API by name and header path so Claude verifies against the installed versions.
3. Structural checks keep the plugin valid, small, generic and free of GMC source.

Non-goals (for this iteration):

- Agents, slash commands, MCP toolsets or hooks that run tools. The plugin is knowledge plus one detection hook.
- Compile or runtime CI (GMAS needs the paid GMC plugin).
- Blueprint-only variants of every skill; authoring skills carry Blueprint notes where the Blueprint path differs.
- Behavioral evaluation of the skills (`claude plugin eval`); structural checks only, by decision.

## 3. Layout and distribution

```
.claude-plugin/marketplace.json          marketplace at the repository root
Claude/gmas/
  .claude-plugin/plugin.json
  README.md
  hooks/hooks.json
  hooks/gmas-context.sh
  hooks/tests/gmas-context.test.sh
  scripts/check.sh
  skills/<skill>/SKILL.md                twelve skills, see §5
  skills/<skill>/references/*.md         long tables and recipes, where a skill needs them
```

`.claude-plugin/marketplace.json`:

```json
{
  "name": "reznok",
  "owner": { "name": "reznok" },
  "metadata": { "description": "Claude Code plugins for GMAS (GMC Ability System) and related Unreal plugins" },
  "plugins": [
    {
      "name": "gmas",
      "source": "./Claude/gmas",
      "description": "GMC-aware skills for authoring, debugging, reviewing and testing GMAS gameplay",
      "category": "development"
    }
  ]
}
```

`Claude/gmas/.claude-plugin/plugin.json`:

```json
{
  "name": "gmas",
  "displayName": "GMAS",
  "version": "1.4.0",
  "description": "GMC-aware skills for authoring, debugging, reviewing and testing GMAS (GMC Ability System) gameplay",
  "author": { "name": "reznok" },
  "homepage": "https://github.com/reznok/GMCAbilitySystem",
  "repository": "https://github.com/reznok/GMCAbilitySystem",
  "license": "MIT",
  "keywords": ["unreal", "gmc", "gmas", "ability-system", "prediction", "multiplayer"]
}
```

Install:

```
/plugin marketplace add reznok/GMCAbilitySystem
/plugin install gmas@reznok
```

The marketplace is read from the repository's default branch (`main`), so the plugin reaches users with each promotion of `dev` to `main`. `/plugin marketplace add reznok/GMCAbilitySystem@dev` follows the integration branch. The marketplace is named after the owner so plugins from other repositories can be listed later through `github` sources.

Versioning: `version` tracks the GMAS release the content describes (`1.4.x`); the patch component bumps for documentation fixes. Claude Code updates an installed plugin only when this string changes, so every content change that reaches `main` bumps it.

## 4. The detection hook

`hooks/hooks.json` registers one `SessionStart` hook (matcher `startup|resume|clear|compact`, `bash "${CLAUDE_PLUGIN_ROOT}/hooks/gmas-context.sh"` — quoted, as `claude plugin validate --strict` requires — timeout 5 s). Bash is required (Git Bash on Windows), as for Epic's Unreal Engine plugin hook.

`gmas-context.sh`:

1. Exits silently when `CLAUDE_GMAS_HOOK_DISABLE` is set.
2. Walks up from `$PWD` to the first directory holding a `*.uproject` (the project root). A `*.uproject` anywhere up the chain wins, so a session started inside a GMAS submodule of a project is treated as that project's session. Only when the whole walk finds no `*.uproject` but did pass a directory holding `GMCAbilitySystem.uplugin` (a standalone checkout of this repository) does the script emit the maintainer context (step 6) and exit.
3. Finds, under `<root>/Plugins` up to three folders deep (`find -mindepth 2 -maxdepth 4`), `GMC.uplugin` and `GMCAbilitySystem.uplugin`. The folder name is irrelevant (vendored copies and submodules use different names). The first match in sorted order wins. The walk never examines the filesystem root (enumerating `/` on MSYS can stall for tens of seconds).
4. Reads `VersionName` from each `.uplugin` with `sed`. GMAS generation: `Source/GMCAbilitySystem/Public/Utility/GMASBoundQueueV2.h` next to the uplugin → `1.4+ bound queue V2`, else `pre-1.4: bound queue V1, skill notes tagged 1.4+ do not apply`. The version is labelled `VersionName` because a tree can be newer than its uplugin string (vendored copies of the 1.4 tree still said 1.3).
5. Emits `{"hookSpecificOutput":{"hookEventName":"SessionStart","additionalContext":"..."}}` with one paragraph, paths relative to the project root:

   > This project uses GMC (General Movement Component) at `Plugins/GMC` (2.3.9) and GMAS (GMC Ability System) at `Plugins/GMCAbilitySystem` (VersionName 1.4, 1.4+ bound queue V2). GMC's source is licensed and exists only inside this project: read `Plugins/GMC/Source/GMCCore/Public/...` for exact signatures and never copy GMC code into other repositories. Load `gmas:gmas-rules` before editing predicted gameplay (`gmas:gmc-prediction` for GMC-only code); author with `gmas:gmas-ability`, `gmas:gmas-effect`, `gmas:gmas-attribute`, `gmas:gmas-task`; use `gmas:gmas-debug` for desync, replay or missing-effect issues, `gmas:gmas-review` for reviews, `gmas:gmas-testing` for automation tests, `gmas:gmas-setup` when wiring a new pawn, `gmas:gmas-upgrade` after updating GMAS.

   GMAS found but GMC missing: the GMC clause becomes "GMC (General Movement Component), which GMAS requires," and the source sentence "GMC was not found under `Plugins/`; it may be installed as an engine plugin (Engine/Plugins/Marketplace). GMC's source is licensed: read its headers where they are and never copy GMC code into other repositories." Nothing found: no output.
6. Maintainer context (checkout of this repository): "This is the GMAS plugin repository. Load `gmas:gmas-maintain` for the branch model, the DeepWorlds sync, the specs and the release recipe. Content here must stay generic."
7. `CLAUDE_GMAS_HOOK_DEBUG` set → tracing on stderr. JSON-escapes backslashes and quotes.

The hook is tested by `hooks/tests/gmas-context.test.sh`: fixture trees under a temp directory (no project; project without plugins; project with a pre-1.4 GMAS copy and no GMC; project with GMC and GMAS in a submodule-named folder, started from a subdirectory; a clone of this repository; the disable variable). Each case asserts the exact substrings expected in `additionalContext` or silence, and that the output parses as JSON. Final line `N passed, 0 failed`.

## 5. Skills

Common rules for every `SKILL.md`:

- Frontmatter: `name` equals the directory name; `description` states only *when* to use the skill ("Use when …"), never its workflow; frontmatter ≤ 1024 characters.
- ≤ 300 lines. Long tables and recipes live in `references/*.md` and are linked.
- Opens with a two-line notice: behavior verified against GMC 2.3.x and GMAS 1.4; confirm signatures in the project's own headers (paths given relative to each plugin root, for example `Source/GMCCore/Public/Components/GMCReplicationComponent.h`), GMC's source is not part of this plugin.
- Examples use neutral names: `AMyPawn`, `UMyMovementCmp`, `UMyAbility_Dash`, `Ability.Dash`, `Attribute.Stamina`, `State.Stunned`.
- Items that differ on GMAS copies older than 1.4 are tagged `1.4+`.
- Authoring skills end with a checklist and "which test to write", and link `gmas-rules` rather than restating constraints.

### 5.1 `gmc-prediction`

Description: *Use when writing or reviewing code that runs inside GMC's movement cycle (GenPredictionTick, bound values, replays, combined moves, simulated proxies, move timestamps) or when a GMC pawn behaves differently on client and server.*

Content: the move cycle (local move → `GenPredictionTick`; the server re-executes client moves; simulated proxies run `GenSimulationTick` and smoothing; `GenAncillaryTick(DeltaTime, bLocalMove, bCombinedClientMove)`; `PreLocalMoveExecution`); roles (`IsAutonomousProxy`, `IsSimulatedProxy`, `IsLocallyControlledServerPawn`, `IsRemotelyControlledServerPawn`); binding (`Bind*` family inside `BindReplicationData_Implementation`, order defines the wire layout, combine modes); replay (`CL_IsReplaying`, `IsSimulatedMove`, combined moves re-run the tick from the move's start, `CL_DoNotCombineNextMove()` after one-shot events, side effects only on live non-replayed moves); time (`GetMoveTimestamp`, no clock sync without one `AGMC_WorldTimeReplicator` in the world); smoothing (`PostSpawnSmoothingPause`, bound values reach simulated proxies late); `OnSyncDataApplied` carries historical values on rollback, so detect state changes in `WorldTickEnd`. `references/gmc-diagnostics.md`: the `gmc.*` console variables and `LogGMC*` categories. Links to the GMC documentation.

### 5.2 `gmas-rules`

Description: *Use before changing gameplay that involves a GMC_AbilitySystemComponent: deciding where state lives (bound, replicated, derived), attribute binding, ability lifetime under replay, effect queue types, server operations, impulses and tags.*

Content: the ASC forwards the five GMC hooks and binds last; a "where does this state live" decision table (GMC-bound / replicated / derived per machine / local cosmetic); attributes (`bGMCBound` only for values predicted logic reads, clamps set deliberately, data in place before the ASC binds, change bound attributes through effects or `ApplyAbilityAttributeModifier`, never a direct set); abilities (tag activation, `QueueAbility`, `bActivateOnMovementTick`, ability instances and `ActiveCooldowns` are not rewound on replay → long-lived instances that poll bound flags, cooldowns as bound floats when predicted logic reads them, `WaitForInputKeyRelease` needs an Enhanced Input action); effects (`Predicted`, `PredictedQueued`, `ServerAuth`, `ServerInstantAttribute` 1.4+, `bUniqueByEffectTag`, `Set`/`SetReplace` layering); server operations (queued into the target's move; immediate vs deferred application for targets without an autonomous client; the ~1 s grace force when a batch fails tag checks); `AddImpulse` through the ASC (server-authored, applied inside the target's move; replace velocity = desired − current with `bVelChange`); synced events; bound state tags; the cosmetic multicast shape (server multicasts, receiver skips authority and the predicting owner); never `DOREPLIFETIME` predicted state; identity and configuration through initial-only replication plus bound seeding.

### 5.3 `gmas-setup`

Description: *Use when adding GMAS to a project or wiring a new GMC pawn or movement component to a GMC_AbilitySystemComponent, in C++ or Blueprint, including submodule install and Build.cs dependencies.*

Content: requirements (GMC license, UE 5.5–5.8 — 5.4 unverified because `Utility/GMASBoundQueueV2.h` includes `StructUtils/InstancedStruct.h` without the version guard — Niagara, GameplayTags); install as a submodule (`-b main` for releases, `-b dev` to follow integration) or as a copied folder; `.uproject` plugin entries; `Build.cs` dependencies (`GMCCore`, `GMCAbilitySystem`, `GameplayTags`); C++ wiring (`AGMC_Pawn`, a movement component subclass forwarding the five hooks, `UGMC_AbilitySystemComponent`, `BindReplicationData` with the ASC last, a `UGMCAttributesData` asset, tags `Ability.*` / `Attribute.*`, ability map, starting abilities and effects); the Blueprint path (link to the wiki's Getting Started); a first ability sketch.

### 5.4 `gmas-ability`

Description: *Use when creating or modifying a UGMCAbility: lifecycle hooks, cost and cooldown, activation tags, chains, tasks, input, and its visuals and sounds on every machine.*

Content: anatomy (`AbilityTag`, `AbilityCost`, `CooldownTime`, `bApplyCooldownAtAbilityBegin`, `bAllowMultipleInstances`, `ActivationRequiredTags`, `ActivationBlockedTags`, cancel and block containers, `bBlockAllOtherAbilities` with its allow-list, `ApplyEffectOnEnd`, `RemoveEffectOnEnd`, chains: `ChainWindowTag`, `ChainWindowDuration`, `ChainConsumeWindowTags`, `bActivateOnMovementTick`, `ServerConfirmTimeout`, definition tags and queries); lifecycle (`PreBeginAbility` → `BeginAbility` → `Tick` / `AncillaryTick` → `EndAbility` or `CancelAbility`; `CanAffordAbilityCost`, `CommitAbilityCost`, `CommitAbilityCostAndCooldown`, `RemoveAbilityCost`); tasks and where they tick; wiring (ability map, `StartingAbilities`, `GrantedAbilities` on effects, `AbilityInputAction`, input tags). Recipes in `references/recipes.md`: instant (impulse), held (apply effect, poll input, remove on end), charge or cast with cost at commit, a per-life persistent ability polling a bound input flag (replay-safe), a chained combo, an AI-queued ability (`QueueAbility` from the controller tick). **Presentation on every machine** (mandatory section): predicted vs multicast spawning with `SpawnParticleSystemAtLocation` / `SpawnSound`, skip on authority and on the predicting owner, FX only on live moves, the simulated-proxy smoothing delay option, Niagara user parameters, montages through `GMCAbilityAnimInstance` / `WaitForGMCMontageChange`, synced events for one-shot cues; API table in `references/cosmetics.md`. Checklist: side effects only on live moves, no instance state the replay cannot rebuild, cooldown placement, cost effect is instant, tags registered, which test to write.

### 5.5 `gmas-effect`

Description: *Use when creating or modifying a UGMCAbilityEffect or FGMCAbilityEffectData: duration and periodic settings, granted tags, modifiers and conditions, queue type, runtime-built effects, removal.*

Content: anatomy (`EffectType`, `Duration`, `Delay`, `PeriodicInterval`, `bPeriodicFirstTick`, `bNegateEffectAtEnd`, `EffectTag`, `GrantedTags`, `bPreserveGrantedTagsIfMultiple`, `bUniqueByEffectTag` 1.4+, application and ongoing `MustHave` / `MustNotHave` tags, `GrantedAbilities`, `PauseEffect`, `CancelAbilityOnActivation`, `CancelAbilityOnEnd`, `ApplyEffectOnEnd`, `RemoveEffectOnEnd`, `ClientGraceTime`, `bServerAuth`, `bClientAuth` 1.4+, `Modifiers`, definition queries); hooks (`StartEffect`, Effect Tick, Period Tick, Dynamic Condition, `OnAttributeModifierApplication` with `bCallOnAttributeModifierApplication`, `EndEffect`); modifiers (`EModifierType` table, `ValueType`: `AMT_Value` / `AMT_Attribute` / `AMT_Custom` / `AMT_External` 1.4+, `Conditions` with Skip or OverrideValue and `bReevaluateConditionsWhilePersistent` 1.4+, the layering order, `MetaTags`); choosing the queue type (who applies it, where, grace behavior); runtime-built effects (`ApplyAbilityEffect` with data, `ApplyAbilityEffectSafe`), removal by handle, tag or query. Recipes in `references/recipes.md`: cost, damage or heal over time, timed buff with negate, stun (granted tag blocks abilities, cancels on activation), unique-replace buff, server-authoritative damage, external-value modifier. Checklist and test pattern.

### 5.6 `gmas-attribute`

Description: *Use when adding or changing GMAS attributes: GMCAttributesData rows, clamps, GMC-bound versus replicated, reading and modifying values at runtime, custom modifier calculators.*

Content: `UGMCAttributesData` rows (`AttributeTag`, `DefaultValue`, `bStartFull` 1.4+, `Clamp` with `bClampMin` / `bClampMax` 1.4+, `Min` / `Max`, `MinAttributeTag` / `MaxAttributeTag`; the default clamp is [0, 0], so set `Max` or untick `bClampMax`), `bGMCBound`, `ValueCombineMode` 1.4+; bound vs unbound (predicted reads only, bandwidth, the combine-mode contract); tag conventions; several data assets per component; data in place before the ASC binds. Runtime access: `GetAttributeValueByTag`, `GetAttributeByTag` (`Value` vs `RawValue`), `ApplyAbilityAttributeModifier`, why `SetAttributeValueByTag` is wrong for bound attributes at runtime, `OnAttributeChanged` / `OnPreAttributeChanged` (historical values under replay; presentation only). Patterns: a max attribute as `MaxAttributeTag`, regeneration through a periodic effect, derived stats through `AMT_Attribute` modifiers, custom calculators (`UGMCAttributeModifierCustom_Base::Calculate`, the bundled exponent). Checklist, and the known standalone-struct spec issue.

### 5.7 `gmas-task`

Description: *Use when creating or modifying a GMAS ability task (UGMCAbilityTaskBase subclass): target data, waits, heartbeat, tick placement, replay safety.*

Content: anatomy of `UGMCAbilityTaskBase` and `GMCAbilityTaskData`; the built-in tasks as models (`WaitDelay`, `WaitForInputKeyPress` / `Release` / `Parameterized`, `WaitForGameplayTagChange`, `WaitForGMCMontageChange`, `WaitForRotateYawTowardsDirection`, `SetTargetData*`); client and server halves, the heartbeat watchdog (real time, 1.4+), FIFO task data, which tick a task runs in, timeout pins, replay safety (no live input polls on replay), ending and cleanup order. Recipe: a new `SetTargetData` task and a new wait task. Checklist and test.

### 5.8 `gmas-debug`

Description: *Use when GMAS or GMC gameplay misbehaves: desync, double activation, late or missing effects, attributes stuck at zero, replay oddities, crashes in the ability component; covers logs, console variables, the debugger and a symptom table.*

Content: surfaces (log categories `LogGMCAbilitySystem`, `LogGMCReplication`, `LogGMCMovement`, `LogGMCPawn`, `LogGMCController`; `Log <category> Verbose`; the Verbose trace tags emitted by the component, names confirmed from source; `GMAS.LogApplyTrace` / `GMAS.ApplyTraceFilter` and `BL.GMAS.DumpAttrBindMap` 1.4+; the `gmc.*` variables; the Gameplay Debugger category `GMCAbilitySystem`; Network Timing and Replay Burst settings 1.4+); a procedure (reproduce in PIE with network emulation, align server and client logs by move timestamp, reduce to standalone, run the `GMAS.*` specs). `references/symptoms.md`: symptom → likely cause → how to check → fix, covering at least: attribute reads 0 or will not change; an ability fires twice or its FX play twice; activation lands about a second late; an effect is missing on one client; a simulated proxy shows a stale bound value; an ability is cancelled by its own cooldown after a replay; a key-release task ends immediately; odd values in `OnSyncDataApplied`; server-pawn activations dropped or a standalone crash on pre-1.4 copies; "Forcing Operation" spam; operation id mismatches.

### 5.9 `gmas-review`

Description: *Use when reviewing a change that touches GMC or GMAS code, before merging or after a coding agent implements it.*

Content: two passes, spec compliance then quality; the networking checklist (authority checks, replay guards, simulated and combined moves, bound vs replicated, binding order, clamps, side effects only on live moves, cooldown storage, no `DOREPLIFETIME` of predicted state, presentation on every machine); UE hygiene (`UPROPERTY` on UObject references, shadowing warnings treated as errors by MSVC, clang's single-iteration loop warning, includes); tests present; edits to vendored plugins documented; no GMC code copied. Report format: Spec ✅/❌ with numbered issues; Quality with Critical / Important / Minor; `file:line` citations.

### 5.10 `gmas-testing`

Description: *Use when writing or running automation tests for GMC/GMAS gameplay: headless GMAS specs or networked play-in-editor tests with a dedicated server and clients.*

Content: headless specs (`BEGIN_DEFINE_SPEC` style used under `Source/GMCAbilitySystem/Private/Tests`, test ability and effect classes, running `GMAS.*` with `UnrealEditor-Cmd … -ExecCmds="Automation RunTests GMAS;Quit" -unattended -nullrhi`, filters); networked PIE tests (a latent command with phases Start / WaitReady / Run / End / WaitClosed; `ULevelEditorPlaySettings` for a dedicated server, client count, one process and network emulation; `GEditor->RequestPlaySession`; worlds re-resolved every frame because a cached `UWorld*` dangles across sessions; readiness plus settle time; confirm windows that wait for a condition or a timeout and assert once; serial execution because sessions share a port); a generic skeleton `FMyNetTest` in `references/net-test-skeleton.md`; what to assert for predicted gameplay (server and client agree after the confirm window; no replay explosion).

### 5.11 `gmas-upgrade`

Description: *Use when moving a project to a newer GMAS (for example 1.3 to 1.4) or when a build breaks after updating the GMAS submodule.*

Content: a per-release migration table starting with 1.3 → 1.4: bound queue V1 → V2 operations, `ApplyAbilityEffect` signature and effect handles, `ServerAuthMove` removal, explicit clamp flags, attribute layering, changed signatures, the Niagara dependency, `StructUtils` include layout; how to find the installed generation; a verification pass (build, `GMAS.*` specs, a PIE session). Seeded from the 1.4 release notes and kept in step with future releases.

### 5.12 `gmas-maintain`

Description: *Use when working inside the GMAS repository itself: branches, the DeepWorlds sync, resolving a sync conflict pull request, running the GMAS specs, promoting and releasing.*

Content: the `dev` / `main` / `sync/deepworlds` model (from `docs/BRANCHING.md`), resolving a conflict pull request locally, running the sync script dry, the specs and the known failing set, the promotion and release recipe, the generic-content rule and the leak check, where tests live, how the Claude plugin is validated (`scripts/check.sh`) and versioned.

## 6. Content policies

- **Generic.** No downstream project names, no drive or user paths, no game asset names. Checked by grep (§7).
- **GMC licensing.** GMC appears by API name, a one-line signature and described behavior. No GMC code blocks, no copied comments, no GMC files, no GRIMTEC copyright lines. Every GMC reference names the header path under the project's GMC plugin and, where useful, the GMC documentation.
- **Versions.** Content describes GMAS 1.4 and GMC 2.3.x. Differences on older GMAS copies are tagged `1.4+`; the hook reports the installed generation.
- **Size and triggers.** `SKILL.md` ≤ 300 lines, long material in `references/`; descriptions are triggers only.
- **Seeds.** Content comes from the GMC documentation, the GMAS wiki, this repository's source and tests, and the maintainer's downstream experience. Nothing in the plugin cites a downstream project.

## 7. Validation

`Claude/gmas/scripts/check.sh` (bash; Git Bash and ubuntu) runs:

1. `claude plugin validate --strict Claude/gmas` and `claude plugin validate --strict .claude-plugin/marketplace.json` (the marketplace at the root).
2. Frontmatter: `name` equals the directory; `description` starts with `Use when`; frontmatter block ≤ 1024 characters.
3. Each `SKILL.md` ≤ 300 lines; every relative link in the plugin resolves; `hooks/hooks.json` parses.
4. Leak grep over `Claude/` and `.claude-plugin/` for downstream project names (list kept inside the script), `[A-Z]:/` and `[A-Z]:\\` drive paths, and `/Users/`; any hit fails.
5. GMC excerpt guard: no `GRIMTEC` or GMC copyright lines; no fenced code block that defines a GMC symbol (`GMCCORE_API`, `UGMC_ReplicationCmp::`, `UGMC_MovementUtilityCmp::` followed by a body).
6. The hook harness (`hooks/tests/gmas-context.test.sh`).

The script prints one line per check and ends with `N passed, 0 failed`. It runs locally before any change under `Claude/` is pushed; there is no CI for it by decision.

Acceptance for the first release of the plugin:

- `check.sh` passes.
- Installing from a local marketplace (`claude plugin marketplace add <path to a clone>`) in a downstream project session shows the hook context with the right paths and versions, lists the twelve skills under `/plugin`, and each `/gmas:<skill>` loads.
- The leak grep over the plugin's git history (`git log -p -- Claude .claude-plugin`) is clean.

## 8. Documentation

- `Claude/gmas/README.md`: install commands, the twelve skills with one-line triggers, what the hook injects and how to disable it, the GMC license note, following `@dev`.
- Repository `README.md`: a "Claude Code plugin" section with the two install commands and a link to `Claude/gmas/README.md`.
- `docs/BRANCHING.md`: the plugin ships with promotions; a change confined to `Claude/` or `.claude-plugin/` may be cherry-picked onto `main` and pushed directly, with the plugin version bumped.

## 9. Follow-ups (not in this design)

- Downstream projects adopt the plugin and trim GMAS-generic material from their own instructions.
- Other repositories (for example a StateTree ↔ GMAS plugin) list their plugins in the `reznok` marketplace through `github` sources.
- Agents, commands or evaluation suites, if the skills prove worth automating further.

## 10. Implementation notes (2026-10-03)

Delivered on `dev` as a series of commits after `15676bf`: manifests, `scripts/check.sh` with its harness, the hook with its harness, the twelve skills (each reviewed twice), the docs, and a polish pass. Deviations from the text above, all adopted into the sections they touch:

- **Hook.** The `hooks.json` command quotes `${CLAUDE_PLUGIN_ROOT}` (strict validation warns otherwise). The plugin search runs `find -mindepth 2 -maxdepth 4` under `Plugins/`, takes the first match in sorted order, and never examines the filesystem root (globbing `/` on MSYS took ~34 s on one machine, which would have hit the 5 s timeout in every non-Unreal session). The generation phrase labels the version as `VersionName` because vendored copies of the 1.4 tree still carry `1.3`. The harness asserts the hook's exit code in every case and reports failures instead of aborting.
- **Check script.** `GMAS_CHECK_FAST=1` skips manifest validation and the hook harness so the check harness runs in about two minutes instead of fifteen; skips are counted separately and never hide a failure. The GMC-excerpt guard resets its fence state per file and recognises indented and `~~~` fences; the file scan is sorted so the harness's ordering assumptions hold on every filesystem. The two checker scripts are the only files excluded from the leak grep (they carry the patterns and the injected violations).
- **Skills.** Where this spec's outlines named a fact the headers contradicted, the headers won: `EGMASEffectType` is `Instant`/`Ticking`/`Persistent`/`Periodic`; `SetAttributeValueByTag`, `ExecuteSyncedEvent`, `GetQueuedAbilityCount` and `OnPreAttributeChanged` are inert on 1.4 and are documented as such; the grace-time deferral of predicted removals is disabled by a source-level override; every abnormal end path except `CancelAbility` is a natural end; `CanAffordAbilityCost` is never called by the activation path; the ASC's movement-component pointer must be set by the consumer before binding; `bActivateOnMovementTick` selects where activation and task payloads are dispatched, not where the ability ticks; the ability map is keyed by `Input.*` tags. Items that differ on 1.3 trees carry `(1.4+)` after a `git grep` against `v1.3.0`.
- **Audit by-product.** Verifying the skills against the source surfaced over a hundred defects and documentation drifts in GMAS itself (dead public APIs, flipped defaults, the test helpers behind the 150 failing specs, unchecked client payloads on the server). They are tracked outside this spec and partly reflected in the 1.4.0 release notes' known issues.
- **Acceptance.** `bash Claude/gmas/scripts/check.sh` → `57 passed, 0 failed`; `scripts/tests/check.test.sh` → `14 passed, 0 failed`; `hooks/tests/gmas-context.test.sh` → `18 passed, 0 failed`; the hook run from a downstream project's source folder named both plugins with their versions and generation; the leak grep over every plugin commit's diff and message is clean.
