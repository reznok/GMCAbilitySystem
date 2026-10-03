---
name: gmas-maintain
description: Use when working inside the GMAS repository itself - branches, the DeepWorlds sync, resolving a sync conflict pull request, running the GMAS specs, promoting and releasing.
---

> Behavior below was verified against GMC 2.3.x and GMAS 1.4. Confirm signatures in this project's own headers (paths are relative to each plugin's root, e.g. `Source/GMCCore/Public/Components/GMCReplicationComponent.h`); GMC's source is not part of this plugin.

This skill is for a checkout of `reznok/GMCAbilitySystem` (the SessionStart hook says "This is the GMAS plugin repository"), not for a project that consumes GMAS: that is `gmas:gmas-setup` and `gmas:gmas-upgrade`. The repository has three branches, one automated job, no compile CI (GMAS depends on the paid GMC plugin, so every build happens on the maintainer's machine), a spec suite that runs headless from a downstream project, and this Claude Code plugin under `Claude/gmas/`. `docs/BRANCHING.md` is the operative document; this skill condenses it and adds the plugin. Paths are relative to the repository root unless noted.

## Branches

| Branch | Role | Who writes |
|---|---|---|
| `dev` | Unstable integration. Receives the sync job's merges from the DeepWorlds fork and the maintainer's own pushes (after a local build). May be red on a given day. What submodule consumers follow (`-b dev`). | sync job, maintainer |
| `main` | Stable. Promoted from `dev` by hand after a local build; releases are annotated tags `vX.Y.Z` on it. The default branch, so GitHub's scheduled workflows and the Claude Code marketplace read from it. Protected (review and last-push approval; administrators exempt). | maintainer |
| `sync/deepworlds` | Automation-owned. Exists only while a conflict pull request is open; force-pushed so it always equals the fork's `dev` tip. | sync job |

Fork commits are merged, never cherry-picked, so every later merge stays small and a merge commit records what arrived; an unwanted fork change is `git revert`ed on `dev` afterwards. Your own change: build locally, push `dev`; if the push is rejected because a sync landed meanwhile, pull and push again. Consumers add GMAS as a submodule (`gmas:gmas-setup` has the commands: `-b dev` to follow integration, `-b main` for releases only); the plugin's internal name `GMCAbilitySystem` does not depend on the folder name. Fixes go upstream, then the consumer bumps its submodule pointer; no patch lives inside a submodule.

## The DeepWorlds sync

`.github/workflows/sync-deepworlds.yml` runs daily at 06:00 UTC and on demand (Actions tab, *Run workflow*, with a `dry_run` box). It checks out `dev` with full history and runs `.github/scripts/sync-deepworlds.sh` with `GH_TOKEN` set to the job token (`contents`, `pull-requests` and `issues: write`; labels live in the Issues API). GitHub runs `schedule` triggers from the default branch only, so the workflow and `.github/scripts/` must exist on `main` as well as on `dev`; whichever copy runs, it operates on `dev`. The script fetches `DeepWorldsSA/DeepWorlds_GMCAbilitySystem` `dev` and does one of three things:

| Outcome | What happens |
|---|---|
| **Up to date** (the fork tip is already in `dev`) | Nothing is pushed. An open conflict PR is closed as superseded and `sync/deepworlds` is deleted. |
| **Clean merge** | A `--no-ff` merge is pushed to `dev` as `Sync DeepWorlds dev: N commits (<base>..<tip>)`; the body lists the fork commits (first 50) and the **overlap**: files both sides touched since the last sync, the heads-up that fork work landed next to local work. A push rejected because `dev` moved is refetched and retried once. |
| **Conflict** | The merge is aborted; the fork tip is force-pushed to `sync/deepworlds`; one PR (head `sync/deepworlds`, base `dev`, label `deepworlds-sync`) is created, or the existing one re-titled and re-bodied, with the conflicting files, the overlap, the commits and the resolve recipe. |

All three exit 0; only fetch, push-after-retry and `gh` failures fail the job (and email the maintainer). Environment, defaults in parentheses: `UPSTREAM_URL` (the fork), `UPSTREAM_BRANCH` (`dev`), `UPSTREAM_REMOTE` (`deepworlds`), `TARGET_BRANCH` (`dev`), `SYNC_BRANCH` (`sync/deepworlds`), `DRY_RUN` (`false`); `GH_TOKEN` is required unless `DRY_RUN=true`; `GH_REPO` is set by the workflow (`owner/repo`) so `gh` targets this repository, while a local run lets `gh` infer it from `origin`; the Markdown report goes to `GITHUB_STEP_SUMMARY` when set, else to stdout. Preconditions it checks: `TARGET_BRANCH` checked out, clean tree, full (not shallow) clone.

**Dry run locally**, in a full clone of `dev` with a clean tree and network. It adds or re-points a `deepworlds` remote in your clone, fetches, reports, pushes nothing and opens nothing; a trial merge is reset to `ORIG_HEAD`:

    DRY_RUN=true bash .github/scripts/sync-deepworlds.sh

Expected while the fork is quiet: `## Sync DeepWorlds: up to date`. Otherwise `dry run, would merge N commits (…)` with the overlap list, or `dry run, N commits conflict in M files` with the files.

**Harness.** `bash .github/scripts/tests/sync-deepworlds.test.sh` builds throwaway repositories under a temp dir and stubs `gh` with a shim that logs its calls; no network. T1 to T7 cover up to date, clean merge dry and live, conflict dry and live, PR update, a manual resolve closing the PR, a push rejected once and a real non-fast-forward race. Final line `41 passed, 0 failed`. Run it after any change to the script, and add a case for every new branch of the script.

**Resolving a conflict PR.** Never press *Merge* on GitHub: the PR carries the unresolved fork tip and exists only to hold the report and the branch. Resolve in your clone:

    git fetch origin dev sync/deepworlds
    git checkout dev && git pull --ff-only
    git merge --no-ff origin/sync/deepworlds
    # resolve conflicts, build locally, then
    git push origin dev

Keep the fork's version of a shared fix and every local addition; the merge commit is where that choice is recorded. The next run finds the fork tip in `dev`, closes the PR and deletes the branch. If the fork moves while the PR is open, the branch is force-updated and the title and body recomputed; resolve against the newer tip.

## Specs

Tests live in `Source/GMCAbilitySystem/Private/Tests/`: seventeen `GMAS_<Area>Spec.cpp` files (449 cases under `GMAS.*` on 1.4.0) and the `UGMAS_Test*` stub classes they configure through class defaults (`UGMAS_TestAbility`, `UGMAS_TestCostEffect`, `UGMAS_TestMovementCmp`, …). Each spec is a `BEGIN_DEFINE_SPEC` / `END_DEFINE_SPEC` block guarded by `#if WITH_AUTOMATION_WORKER`, flags `EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter`, named `GMAS.Unit.<Area>` (`GMAS.Stress.*` for the combinatoric ones); the gameplay tags they register are `GMAS.Test.*`, `GMAS.BugFix.*` and `GMAS.Cond.*`. `GMCAbilitySystem.Build.cs` adds `Private/Tests` to `PublicIncludePaths` so a downstream test module can include the stubs. This repository has no runner: the specs need an engine and a project with GMC enabled.

**Headless run**, from a downstream project with GMC and GMAS enabled, editor closed:

    UnrealEditor-Cmd.exe <Project>.uproject -ExecCmds="Automation RunTests GMAS;Quit" -unattended -nullrhi -log

One `LogAutomationController` line per test and a summary land in `Saved/Logs/<Project>.log`; `-ReportExportPath=<dir>` writes the JSON and HTML report. Narrow with a `+`-joined substring list (`GMAS.Unit.Effect+GMAS.Unit.Duration`). Mechanics and the stub harness: `gmas:gmas-testing`.

**Known failing set (1.4.0).** About 150 cases fail by design: `GMAS.Unit.Attribute*`, `GMAS.Stress.*`, `GMAS.Unit.ModifierMath` and the `GMAS.Unit.Ability` cooldown case. Their helpers build `FAttribute` values with the default `FAttributeClamp` (`bClampMin` and `bClampMax` true, `Min` and `Max` 0: `Public/Attributes/GMCAttributeClamp.h`), which 1.4 applies as a `[0, 0]` pin, so every value reads 0 (`gmas:gmas-attribute`). The runtime is right; the fix belongs in the helpers (set `Max`, or clear the flags), never in `FAttributeClamp`. Until it lands, the promotion gate is "the same pass/fail set as the last promotion": compare the summary with the previous run and treat any name outside the set as a regression. When the set changes, update it in `gmas:gmas-testing`, `gmas:gmas-upgrade`, this section and the release notes' *Known issues*.

**Adding specs.** New behavior ships with a spec in the same folder: `GMAS_<Area>Spec.cpp`, same guard, flags and naming; stub configuration written to class defaults in `BeforeEach` and restored in `AfterEach`; time advanced by writing `ActionTimer` and calling `TickActiveEffects` then `ProcessAttributes(true)` (`GMAS_DurationSpec.cpp` is the model; `GMAS_BugFixSpec.cpp` and `GMAS_ClientAuthSpec.cpp` for server-operation and client-auth paths). When the behavior sits behind a role or replay decision the stub cannot produce, add a `…ForTest` seam at the bottom of `Public/Components/GMCAbilityComponent.h`, inside the existing `#if WITH_AUTOMATION_WORKER` block: a thin wrapper around the private function or a `bForce…ForTest` flag, never a change to the shipped path. Seams prove that a branch runs, not that the network takes it; anything about two machines is a networked PIE test in a downstream project (`gmas:gmas-testing`).

## Promoting and releasing

`main`'s 1.3 history is a squash, so a plain merge of `dev` conflicts; a promotion is a merge commit that carries `dev`'s tree exactly (the 1.4 release is one: two parents, tree identical to `dev`'s). Steps:

1. **Gate.** Build at least one downstream project against `dev` and run the specs headless (above); the failing set equals the known set. `bash Claude/gmas/scripts/check.sh` ends `N passed, 0 failed` (below): the plugin ships with every promotion.
2. **Versions and plugin content, on `dev`.** Bump `VersionName` in `GMCAbilitySystem.uplugin` (`"1.4"` to `"1.5"`; the tag adds the patch). Write the release notes and, from the same header diff (`git diff v1.4.0 origin/dev -- Source/GMCAbilitySystem/Public`), extend `gmas:gmas-upgrade` with a `## 1.4 → X.Y` section (its *Later releases* section says how), re-tag `(1.4+)` facts across the skills where they changed again, update the known failing set in `gmas:gmas-testing`, and the hook's generation probe if the marker header (`Public/Utility/GMASBoundQueueV2.h`) moves. Then bump `version` in `Claude/gmas/.claude-plugin/plugin.json`: at least a patch bump whenever plugin content changes; `X.Y.0` when GMAS's minor version changes. Claude Code updates an installed plugin only when that string changes, and the marketplace is read from `main`, so a promotion without the bump ships nothing to plugin users. The twelve verification notices (`Behavior below was verified against GMC 2.3.x and GMAS 1.4`) are re-verified and bumped with a GMAS minor release and left alone for a patch release. Push `dev`.
3. **Promote.**

       git fetch origin
       NEW=$(git commit-tree origin/dev^{tree} -p origin/main -p origin/dev -m "Release X.Y: promote dev to main")
       git push origin "$NEW":main

   `git rev-parse "$NEW^{tree}" origin/dev^{tree}` prints one hash twice. The push is the maintainer's (administrators are exempt from `main`'s review rule).
4. **Tag and release.** `git tag -a vX.Y.Z "$NEW" -m "GMAS X.Y" && git push origin vX.Y.Z`, then `gh release create vX.Y.Z --notes-file ../gmas-release-notes.md --title "GMAS X.Y"` (write the notes outside the repository tree: an untracked file in the clone fails the sync script's clean-tree check). The 1.4.0 notes are the template (`gh release view v1.4.0 --json body -q .body`): intro and compare link, *Breaking changes*, *Abilities*, *Effects and attributes*, *Tasks*, *Component, diagnostics and tooling*, *Tests*, *Repository*, *Known issues*, *Upgrading*. Every default that flipped and every removed or re-signed public declaration goes under *Breaking changes* and into the `gmas:gmas-upgrade` table; drafts drift (1.4's listed a still-present method), so check each line against `git show vX.Y.0:<path>`.

## The Claude plugin

Layout, fixed by the design (`check.sh` knows the twelve skill names):

    .claude-plugin/marketplace.json           marketplace "reznok" at the repository root; source ./Claude/gmas
    Claude/gmas/.claude-plugin/plugin.json    name gmas, version X.Y.Z (step 2 above)
    Claude/gmas/README.md                     install, the twelve skills, the hook, following @dev
    Claude/gmas/hooks/hooks.json              one SessionStart hook running hooks/gmas-context.sh
    Claude/gmas/hooks/tests/gmas-context.test.sh
    Claude/gmas/scripts/check.sh              the structural check; scripts/tests/check.test.sh is its harness
    Claude/gmas/skills/<skill>/SKILL.md       plus references/*.md for long tables and recipes

Install: `/plugin marketplace add reznok/GMCAbilitySystem` (reads `main`; pin a branch or tag with `owner/repo@ref`, per the Claude Code docs) and `/plugin install gmas@reznok`; the README's *Install* section is the user-facing text.

**Before pushing** anything under `Claude/` or `.claude-plugin/`: `bash Claude/gmas/scripts/check.sh` (optional argument: another repository root). One line per check, final line `N passed, M failed` (`, K skipped` appended in fast mode), exit 1 on any failure:

| # | Check | Fails on |
|---|---|---|
| 1 | `claude plugin validate --strict` on `Claude/gmas` and on the marketplace | unrecognized fields, missing metadata |
| 2 | all twelve expected skills present | a missing `skills/<name>/SKILL.md` (`missing: …` names it) |
| 3 | per skill: frontmatter `name` equals the directory, `description` starts with `Use when`, frontmatter ≤ 1024 chars, `SKILL.md` ≤ 300 lines | any of the four |
| 4 | every relative Markdown link under the plugin resolves | a mistyped `references/` link |
| 5 | `hooks/hooks.json` parses | invalid JSON |
| 6 | leak grep (`LEAK_WORDS`, `LEAK_PATHS` in the script) over the plugin and the marketplace | a downstream project name, a drive path, a home directory |
| 7 | GMC excerpt guard: no GMC copyright lines; no fenced block defining a GMC symbol (`UGMC_ReplicationCmp::…`, `GMCCORE_API`) | a pasted GMC excerpt |
| 8 | the hook harness `hooks/tests/gmas-context.test.sh` (F1 to F8: no project, project without GMAS, pre-1.4 copy without GMC, GMC plus 1.4 submodule from a subdirectory, inside the submodule, this repository, disabled, JSON shape) | any fixture assertion |

`GMAS_CHECK_FAST=1` skips 1 and 8 (the slow ones) and prints a `skip` line for each: use it while iterating on a skill, then run the full check once before the push. `bash Claude/gmas/scripts/tests/check.test.sh` tests the checker itself (S1 to S11: it copies the plugin to a temp tree and injects one violation per scenario); run it after editing `check.sh`.

**Skill conventions** that check 3 cannot see: `description` states triggers only; the two-line notice opens every body; neutral example names (`AMyPawn`, `UMyMovementCmp`, `UMyAbility_Dash`, `Attribute.Stamina`); facts that differ on trees older than 1.4 tagged `(1.4+)`; siblings linked as `gmas:<skill>`; long tables in `references/`; `## Checklist` closes the skill (authoring skills add `## Which test to write` after it). GMAS header paths are written relative to `Source/GMCAbilitySystem/`, GMC's relative to the GMC plugin root.

**A `Claude/`-only fix.** Once the plugin has shipped in a promotion, a change confined to `Claude/` and `.claude-plugin/` does not wait for the next release: commit it on `dev` together with the `plugin.json` patch bump, check green, then put the same commit on `main`:

    git fetch origin
    git checkout --detach origin/main
    git cherry-pick <sha>            # one or more Claude/-only commits from dev
    bash Claude/gmas/scripts/check.sh
    git push origin HEAD:main
    git checkout dev

No tag, no release. The next promotion's `commit-tree` carries `dev`'s tree wholesale, so the duplicate on `main` never conflicts. Anything that touches `Source/` waits for a promotion.

## Rules

- **Generic content only**, in files, commit messages and history: no downstream project names, no drive or user paths, no game features or asset names. Check 6 covers the plugin tree; for `docs/`, `Source/` comments and commit messages run the same patterns before each commit (nothing may print):

      words="$(sed -n "s/^LEAK_WORDS='\(.*\)'$/\1/p" Claude/gmas/scripts/check.sh)"
      paths="$(sed -n "s/^LEAK_PATHS='\(.*\)'$/\1/p" Claude/gmas/scripts/check.sh)"
      git diff --cached | grep -nE "$words|$paths"
      git log --format=%B origin/main..HEAD | grep -nE "$words|$paths"

  The two checker scripts are the only files allowed to contain the patterns. Fork commits arriving through the sync are not rewritten: a leak there is reverted on `dev`, not amended. A lesson learned downstream enters a skill as a generic rule with neutral names, never as the story.
- **GMC source never enters.** GMC is a paid plugin: the repository compiles against its headers but vendors none, and the skills name GMC by API, a one-line signature at most and described behavior, always with the header path relative to the GMC plugin root. No code blocks, comments, files or copyright lines; check 7 catches the detectable part, review catches the rest. Reading GMC headers inside a downstream project to verify a fact is expected.
- **Commit style.** Plain descriptive subject (`gmas skill: gmas-maintain`, `ci: …`, `docs: …`), body only when the why is not obvious, one commit per change, staged by explicit pathspec and read back with `git diff --cached --stat`. Claude-assisted commits carry a `Co-Authored-By` trailer naming the model, currently `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- **LF.** `.gitattributes` normalizes `*.sh`, `*.yml`, `*.json` and `*.md` to `text eol=lf`; the index is LF-only (`git ls-files --eol | grep -E 'i/crlf|i/mixed'` prints nothing; keep it so, and add the extension there when a new file type arrives). Keep the working tree LF too: a CRLF `.sh` fails under bash, so a file written by a Windows editor or tool gets `tr -d '\r'` before it is run or committed.

## Checklist

- You are in a checkout of this repository (`GMCAbilitySystem.uplugin` at the root, no `*.uproject` above it); work inside a consuming project uses `gmas:gmas-setup` or `gmas:gmas-upgrade` instead.
- Own change: built locally, specs run with the failing set equal to the known set, pushed to `dev`; nothing reaches `main` except a promotion or a `Claude/`-only cherry-pick.
- Sync conflict: resolved with the local recipe and pushed to `dev`; the PR was never merged on GitHub.
- Sync script changed: `bash .github/scripts/tests/sync-deepworlds.test.sh` ends `N passed, 0 failed` and `DRY_RUN=true bash .github/scripts/sync-deepworlds.sh` reports sensibly.
- Plugin changed: `bash Claude/gmas/scripts/check.sh` ends `N passed, 0 failed` (full mode, not `GMAS_CHECK_FAST`); `plugin.json` `version` bumped.
- Release: `VersionName` bumped on `dev`; `gmas:gmas-upgrade` extended and `(1.4+)` tags revisited; tree-carrying merge pushed; annotated tag; `gh release create --notes-file` with *Breaking changes* and *Known issues*.
- Every commit: staged diff and message pass the leak grep, no GMC excerpt, trailer on Claude-assisted commits, LF.
