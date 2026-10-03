# gmas — Claude Code plugin for GMAS

Skills and a detection hook that teach Claude Code how GMC (General Movement Component) and GMAS (GMC Ability System) work: where predicted state may live, how abilities, effects, attributes and tasks behave under GMC's replay, how to debug a desync, what to check in a review and how to test it. The plugin lives in the GMAS repository under `Claude/gmas/`, is published through the marketplace manifest at the repository root (`.claude-plugin/marketplace.json`), and contains no GMC source.

## Install

In Claude Code:

    /plugin marketplace add reznok/GMCAbilitySystem
    /plugin install gmas@reznok

The marketplace is read from the repository's default branch, `main`, so the plugin reaches you with each GMAS release and with `Claude/`-only fixes cherry-picked onto `main`. To follow the integration branch instead:

    /plugin marketplace add reznok/GMCAbilitySystem@dev

Claude Code updates an installed plugin when `version` in `.claude-plugin/plugin.json` changes. The hook runs through `bash`: on Windows, install Git for Windows so that Git Bash is on `PATH`.

## What you get

### Twelve skills

Claude loads a skill when a request matches its trigger; you can also load one by name with `/gmas:<skill>` (for example `/gmas:gmas-rules`). Every skill opens with the same notice (verified against GMC 2.3.x and GMAS 1.4; confirm signatures in your own headers), uses neutral example names and ends with a checklist (authoring skills add `## Which test to write` after it). Long tables and recipes sit next to the skill in `references/`.

| Skill | Use when… |
|---|---|
| [`gmc-prediction`](skills/gmc-prediction/SKILL.md) | writing or reviewing code that runs inside GMC's movement cycle (GenPredictionTick, bound values, replays, combined moves, simulated proxies, move timestamps), or a GMC pawn behaves differently on client and server |
| [`gmas-rules`](skills/gmas-rules/SKILL.md) | changing gameplay that involves a GMC_AbilitySystemComponent: deciding where state lives (bound, replicated, derived), attribute binding, ability lifetime under replay, effect queue types, server operations, impulses and tags |
| [`gmas-setup`](skills/gmas-setup/SKILL.md) | adding GMAS to a project or wiring a new GMC pawn or movement component to a GMC_AbilitySystemComponent, in C++ or Blueprint, including submodule install and Build.cs dependencies |
| [`gmas-ability`](skills/gmas-ability/SKILL.md) | creating or modifying a UGMCAbility: lifecycle hooks, cost and cooldown, activation tags, chains, tasks, input, and its visuals and sounds on every machine |
| [`gmas-effect`](skills/gmas-effect/SKILL.md) | creating or modifying a UGMCAbilityEffect or FGMCAbilityEffectData: duration and periodic settings, granted tags, modifiers and conditions, queue type, runtime-built effects, removal |
| [`gmas-attribute`](skills/gmas-attribute/SKILL.md) | adding or changing GMAS attributes: GMCAttributesData rows, clamps, GMC-bound versus replicated, reading and modifying values at runtime, custom modifier calculators |
| [`gmas-task`](skills/gmas-task/SKILL.md) | creating or modifying a GMAS ability task (UGMCAbilityTaskBase subclass): target data, waits, heartbeat, tick placement, replay safety |
| [`gmas-debug`](skills/gmas-debug/SKILL.md) | GMAS or GMC gameplay misbehaves: desync, double activation, late or missing effects, attributes stuck at zero, replay oddities, crashes in the ability component; covers logs, console variables, the debugger and a symptom table |
| [`gmas-review`](skills/gmas-review/SKILL.md) | reviewing a change that touches GMC or GMAS code, before merging or after a coding agent implements it |
| [`gmas-testing`](skills/gmas-testing/SKILL.md) | writing or running automation tests for GMC/GMAS gameplay: headless GMAS specs or networked play-in-editor tests with a dedicated server and clients |
| [`gmas-upgrade`](skills/gmas-upgrade/SKILL.md) | moving a project to a newer GMAS (for example 1.3 to 1.4), or a build breaks after updating the GMAS submodule |
| [`gmas-maintain`](skills/gmas-maintain/SKILL.md) | working inside the GMAS repository itself: branches, the DeepWorlds sync, resolving a sync conflict pull request, running the GMAS specs, promoting and releasing |

### The hook

[`hooks/hooks.json`](hooks/hooks.json) registers one `SessionStart` hook (on startup, resume, `/clear` and compaction; 5 s timeout) that runs [`hooks/gmas-context.sh`](hooks/gmas-context.sh). The script:

1. walks up from the working directory to the first folder that contains a `.uproject`, the project root, so a session started inside a plugin or source subfolder is still that project's session;
2. looks under `<root>/Plugins/` for `GMC.uplugin` and `GMCAbilitySystem.uplugin`, whatever their folders are called (submodules and vendored copies differ; up to three folders deep, first in sorted order);
3. reads `VersionName` from each `.uplugin` and tells the GMAS generation apart by the presence of `Source/GMCAbilitySystem/Public/Utility/GMASBoundQueueV2.h`;
4. injects one paragraph into Claude's context, with paths relative to the project root:

> This project uses GMC (General Movement Component) at `Plugins/GMC` (2.3.9) and GMAS (GMC Ability System) at `Plugins/GMCAbilitySystem` (VersionName 1.4, 1.4+ bound queue V2). GMC's source is licensed and exists only inside this project: read `Plugins/GMC/Source/GMCCore/Public/...` for exact signatures and never copy GMC code into other repositories. Load gmas:gmas-rules before editing predicted gameplay (gmas:gmc-prediction for GMC-only code); author with gmas:gmas-ability, gmas:gmas-effect, gmas:gmas-attribute, gmas:gmas-task; use gmas:gmas-debug for desync, replay or missing-effect issues, gmas:gmas-review for reviews, gmas:gmas-testing for automation tests, gmas:gmas-setup when wiring a new pawn, gmas:gmas-upgrade after updating GMAS.

On an older GMAS copy the generation reads `pre-1.4: bound queue V1, skill notes tagged 1.4+ do not apply`. When GMAS is present but GMC is not under `Plugins/` (an engine-level install, for example), the paragraph says so and still asks Claude to read GMC's headers where they are. In a checkout of this repository itself (no `.uproject` above it) the hook asks for `gmas:gmas-maintain` instead. With no Unreal project above the working directory, or a project without GMAS, it prints nothing.

| Environment variable | Effect |
|---|---|
| `CLAUDE_GMAS_HOOK_DISABLE` | set to any value: the hook does nothing |
| `CLAUDE_GMAS_HOOK_DEBUG` | set to any value: the hook traces what it found on stderr (`gmas-context.sh: ...`) |

The hook needs `bash` on `PATH` (Git Bash on Windows) and is covered by [`hooks/tests/gmas-context.test.sh`](hooks/tests/gmas-context.test.sh).

## GMC

GMC is a paid plugin. Its source exists only inside your project (or among your engine's marketplace plugins), and nothing of it is in this plugin: the skills name GMC by API, give at most a one-line signature, describe behavior, and always cite the header path relative to the GMC plugin root (for example `Source/GMCCore/Public/Components/GMCReplicationComponent.h`) so that you confirm every fact in your own copy. `scripts/check.sh` guards against GMC excerpts entering the plugin. GMC's own documentation is at https://grimtec.net/gmcv2-doc-contents.

## Versions

The content describes GMAS 1.4 and GMC 2.3.x. Facts that differ on GMAS trees older than 1.4 are tagged `(1.4+)` in the skills, and the hook reports which generation your project has, so Claude knows which notes apply. `version` in `.claude-plugin/plugin.json` tracks the GMAS release the content describes (`1.4.x`); the patch component bumps for documentation fixes.

## Contributing

Changes go to `dev` as a pull request. Before opening one:

- Run [`scripts/check.sh`](scripts/check.sh) from the repository root (`bash Claude/gmas/scripts/check.sh`); it must end `N passed, 0 failed`. It needs `bash`, the `claude` CLI on `PATH` (manifest validation fails without it) and python 3 (the `hooks.json` parse and the hook harness). It validates both manifests, checks that the twelve skills are present, that each frontmatter `name` equals its directory and each `description` starts with "Use when", that every `SKILL.md` is at most 300 lines, that relative links resolve, that `hooks/hooks.json` parses, that the leak grep and the GMC-excerpt guard are clean, and runs the hook harness. `GMAS_CHECK_FAST=1` skips the two slow checks while you iterate; run the full check once before pushing. `scripts/tests/check.test.sh` tests the checker itself.
- Keep the content generic: no project names, no drive or user paths, no game assets or features. A lesson learned in a project enters a skill as a rule with neutral example names (`AMyPawn`, `UMyAbility_Dash`, `Attribute.Stamina`), never as the story.
- No GMC source, in any form: API names, one-line signatures, described behavior and header paths only.
- Skill conventions: `description` states triggers only; the two-line verification notice opens the body; long material goes to `references/`; differences on older trees are tagged `(1.4+)`; sibling skills are referenced as `gmas:<skill>`; `## Checklist` closes the skill (authoring skills add `## Which test to write` after it).
- Bump `version` in `.claude-plugin/plugin.json` when the change should reach installed plugins.

To try your change before it is merged, add your clone as a local marketplace in a project session and install `gmas` from it. The clone's marketplace is also named `reznok` and collides with the GitHub one, so remove that first: `claude plugin marketplace remove reznok`, then `claude plugin marketplace add <path to the clone>` and `/plugin install gmas@reznok`; add the GitHub marketplace back when you are done. Maintainers: [`docs/BRANCHING.md`](https://github.com/reznok/GMCAbilitySystem/blob/main/docs/BRANCHING.md) (the plugin ships without `docs/`) and the `gmas-maintain` skill cover promotions and `Claude/`-only fixes.
