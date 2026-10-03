# GMAS: repository reunification and DeepWorlds sync — design

**Date:** 2026-10-02
**Status:** draft for review
**Repo:** `reznok/GMCAbilitySystem` (canonical GMAS, public, MIT)

## 1. Context

GMAS development has split across several places:

| Where | State on 2026-10-02 |
|---|---|
| `reznok/GMCAbilitySystem` `main` | 1.3 (`5a7473b`, 2026-01-07): one squash commit on top of `a13a8ee` (2024-11). Protected: 1 review, last-push approval, admins exempt. |
| `reznok/GMCAbilitySystem` `dev` | `656dba6` (2025-07-28). Strict ancestor of the fork's `dev`. Unprotected. |
| `DeepWorldsSA/DeepWorlds_GMCAbilitySystem` `dev` (the fork) | `5b7f4be` (2026-09-22). 260 commits past our `dev`, 329 past `main`. Carries the spec test suite (`Source/GMCAbilitySystem/Private/Tests/GMAS_*Spec.cpp`, 16 specs), `GMASBoundQueueV2`, ReplayBurst diagnostics, network timing settings. Active. |
| A downstream checkout of the fork lineage, maintained by the GMAS maintainer | 10 commits (May–June 2026) that exist nowhere upstream: a Python ToolsetRegistry toolset for ability authoring, first-class ability chains (combo windows), Niagara user-parameter makers, block-all ability gating, per-tag GrantedTag preservation, three server-operation fixes, the UE 5.8 `StructUtils/` include fix, public `AddImpulse`/`SetActorLocation` declarations. 13 fork commits behind. |

Other downstream projects vendor copies of that checkout with small local patches (log verbosity, a clang `-Wunreachable-code-break` fix, a standalone-mode crash fix). Two of the downstream commits reached canonical only as unmerged branches (`fix/synced-event-public-visibility`, `fix/cooldown-tick-rate`). Nobody has CI. A dry-run merge of the fork into `main` conflicts in 21 files; with the fork's side taken, only seven files of 1.3-only content remain, mostly `GMASBoundQueue.h` V1 (594 lines), superseded by V2.

The maintainer wants `reznok/GMCAbilitySystem` to be the repository they develop in and distribute from, with the fork's work flowing in automatically and their own work pushed directly.

## 2. Goals and non-goals

Goals:
1. One source of truth: everything above merged into `reznok/GMCAbilitySystem` `dev`; nothing left only in a downstream checkout.
2. Fork changes arrive on their own: a daily GitHub Actions job merges the fork's `dev` into our `dev`; a human is needed only on conflict.
3. A documented two-branch model that lets `main` stay a stable, hand-promoted release branch.
4. Downstream projects consume GMAS as a git submodule so drift cannot restart.

Non-goals (deliberate):
- No compile or test CI. GMAS depends on the paid GMC plugin, so compiling needs a self-hosted runner; the maintainer builds locally before promoting to `main`.
- No automatic promotion to `main`, no release packaging (a tag-triggered zip is a cheap follow-up).
- No contribution flow back to the fork.
- Nothing project-specific: GMAS stays a generic plugin. No downstream project names, paths or features appear in this repository's code or docs.

## 3. Branch model

| Branch | Role | Who writes | Protection |
|---|---|---|---|
| `dev` | Unstable integration. Receives fork merges from the sync job and the maintainer's own pushes (after a local build). May be red. | sync job, maintainer | none |
| `main` | Stable. Promoted from `dev` by a pull request after a local build of at least one downstream project. Releases are tags on `main` (`vX.Y.Z`). | maintainer | existing rules unchanged |
| `sync/deepworlds` | Automation-owned. Exists only while a conflict PR is open; always equals the fork's `dev` tip; force-pushed by the job. | sync job | none |

Fork changes are merged, never cherry-picked: the fork's history already contains ours, so after the first merge every later merge is small, and a merge commit records exactly what arrived. Unwanted fork commits are handled by `git revert` on `dev` after the fact. Cherry-picking is reserved for the rare manual case and is not automated.

## 4. One-time reunification (manual, before the job is enabled)

Done in a working clone with `dev` checked out and two extra remotes: `deepworlds` (the fork) and the downstream checkout. Every step is a normal merge with a descriptive message; nothing is squashed or rewritten.

1. **Fork into `dev`.** `git merge deepworlds/dev`: a fast-forward if `dev` carries no local commits, otherwise (this spec's docs commit) a merge commit with no conflicts.
2. **Downstream checkout into `dev`.** Fetch its `HEAD`, `git merge --no-ff FETCH_HEAD`. Expected conflicts: the UE 5.8 `StructUtils/` include fix (both sides made it; keep the fork's wording) and `GMCAbilityComponent.cpp` / `GMASBoundQueueV2*` regions touched by both. Rule: keep every downstream addition (toolset, chains, Niagara param makers, block-all gating, server-op fixes) and the fork's version of shared fixes.
3. **Vendored-copy patches into `dev`.** For each vendored copy, `diff -ru --strip-trailing-cr <copy>/Source dev/Source` and classify every hunk: whitespace, BOM or line-ending only → drop; already upstream → drop; generic fix or diagnostic → one commit upstream; project-specific → stays in that project (none is expected). The known patches: diagnostics lowered from `Warning` to `Verbose` (`[AckTrace:*]` and `[ImpulseTrace]` lines in `ProcessOperation`/`GenPredictionTick`, `RPCOnServerOperationAdded`, which also moves from `LogTemp` to `LogGMCAbilitySystem`); four dead `break;` after `return` removed in `GMCModifierCustom_Exponent.cpp` (Linux clang `-Werror,-Wunreachable-code-break`); the standalone crash fix `IsLocallyControlledListenServerPawn` → `IsLocallyControlledServerPawn` in `GenAncillaryTick`/`GenPredictionTick`; edits to the Python toolset (`Content/Python/gmas_toolset`).
4. **Stray branches.** `fix/cooldown-tick-rate` and `dev-bq-refactor-claude` are already contained in the fork's `dev`: delete after step 1. `fix/synced-event-public-visibility` is the downstream checkout's branch: delete after step 2. Branches last touched in 2024–2025 (`dev-ai`, `dw_dev`, `dw-attribute-refactor`, `dev-bq-refactor`, `dev-effect-application-tags`, `nas-key-task`, `unbound-attributes-patch`, `tag_reference_fix`, `remove-custom-types`) are listed with tip and date for a one-time delete-or-keep decision; nothing is deleted without the maintainer's confirmation.
5. **Housekeeping commits.** `.gitignore` gains `__pycache__/`; `docs/BRANCHING.md` is added (section 6); the workflow and script land (section 5).
6. **Local build gate, then push.** The maintainer builds a downstream project against the reunified plugin and runs the GMAS specs headless: `UnrealEditor-Cmd.exe <Project>.uproject -ExecCmds="Automation RunTests GMAS;Quit" -unattended -nullrhi -log`. Green → `git push origin dev`. The workflow is live from the moment the push lands (it runs on its schedule from `dev`).
7. **`main` promotion** stays manual and later: a PR `dev` → `main` after a local build; `GMASBoundQueue.h` V1 and the other 1.3-only remnants are removed in that PR; tag `v1.4.0`.

## 5. Sync automation

### 5.1 Workflow: `.github/workflows/sync-deepworlds.yml`

```yaml
name: Sync DeepWorlds dev
on:
  schedule:
    - cron: "0 6 * * *"     # daily, 06:00 UTC
  workflow_dispatch:
    inputs:
      dry_run:
        description: "Report what would happen; push nothing, open nothing"
        type: boolean
        default: false
permissions:
  contents: write
  pull-requests: write
  issues: write        # gh label create / --label use the Issues API
concurrency:
  group: sync-deepworlds
  cancel-in-progress: false
jobs:
  sync:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          ref: dev
          fetch-depth: 0
      - name: Configure git identity
        run: |
          git config user.name "github-actions[bot]"
          git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
      - name: Sync
        env:
          GH_TOKEN: ${{ github.token }}
          DRY_RUN: ${{ inputs.dry_run == true }}
        run: bash .github/scripts/sync-deepworlds.sh
```

The workflow never runs on `pull_request`, so no fork-submitted code executes under this repo's token. `GITHUB_TOKEN` is enough because `dev` is unprotected; no personal token is stored. Pull requests the job creates do not trigger other workflows (a `GITHUB_TOKEN` property), which is the desired behavior.

GitHub runs `schedule` triggers only from the default branch, so the workflow file (and `.github/scripts/`) must exist on `main` as well as `dev`; the job always checks out and pushes `dev`, whichever branch's copy runs it. `workflow_dispatch` works from any branch that holds the file.

### 5.2 Script: `.github/scripts/sync-deepworlds.sh`

All logic lives in the script so it can be run and tested in any clone. Bash, `set -euo pipefail`, no dependencies beyond `git` and (outside dry runs) `gh`.

Environment (defaults in parentheses): `UPSTREAM_URL` (`https://github.com/DeepWorldsSA/DeepWorlds_GMCAbilitySystem.git`), `UPSTREAM_BRANCH` (`dev`), `UPSTREAM_REMOTE` (`deepworlds`), `TARGET_BRANCH` (`dev`), `SYNC_BRANCH` (`sync/deepworlds`), `DRY_RUN` (`false`), `GH_TOKEN` (required unless `DRY_RUN=true`), `GITHUB_STEP_SUMMARY` (when set, the report is appended there; otherwise printed to stdout).

Preconditions: the current branch is `TARGET_BRANCH` with full history and a clean tree; `origin` is the canonical repo. The script fails fast with a message if any is false.

Steps:
1. Ensure remote `UPSTREAM_REMOTE` points at `UPSTREAM_URL`; `git fetch "$UPSTREAM_REMOTE" "$UPSTREAM_BRANCH"`.
2. `upstream=$(git rev-parse "$UPSTREAM_REMOTE/$UPSTREAM_BRANCH")`, `base=$(git merge-base HEAD "$upstream")`.
3. **Nothing new** (`git merge-base --is-ancestor "$upstream" HEAD`): report "up to date (fork at `<sha>`)". If an open PR from `SYNC_BRANCH` exists (not dry run): close it with the comment "`dev` already contains the fork tip `<sha>`; superseded." and delete the remote `SYNC_BRANCH`. Exit 0.
4. Gather: `count=$(git rev-list --count "$base..$upstream")`; the commit list `git log --format='- %h %ad %an %s' --date=short "$base..$upstream"` (first 50 lines, then "… and N more"); the **overlap report**: files in both `git diff --name-only "$base" HEAD` and `git diff --name-only "$base" "$upstream"` (`comm -12` on sorted lists).
5. Attempt the merge: `git merge --no-ff --no-edit -m "<message, 5.3>" "$upstream"`.
6. **Clean merge:**
   - dry run → report "would merge N commits, overlap: …", `git reset --hard ORIG_HEAD`, exit 0;
   - otherwise `git push origin "HEAD:$TARGET_BRANCH"`. If the push is rejected (someone pushed `dev` meanwhile): `git fetch origin "$TARGET_BRANCH"`, `git reset --hard "origin/$TARGET_BRANCH"`, repeat from step 2 once; a second rejection fails the job. After a successful push: close any open sync PR with the comment "merged cleanly in `<merge sha>`" and delete the remote `SYNC_BRANCH`. Report the merge sha, commit list and overlap. Exit 0.
7. **Conflict** (merge exits non-zero): `conflicts=$(git diff --name-only --diff-filter=U)`; `git merge --abort`.
   - dry run → report the conflicting files, exit 0;
   - otherwise `git push --force origin "$upstream:refs/heads/$SYNC_BRANCH"` (force is acceptable: the branch is automation-owned and always equals the fork tip), ensure label `deepworlds-sync` exists (`gh label create deepworlds-sync --color C5DEF5 --description "Automated fork sync" --force`), then find an open PR with head `SYNC_BRANCH` and base `TARGET_BRANCH` (`gh pr list --head "$SYNC_BRANCH" --base "$TARGET_BRANCH" --state open --json number`): create it or edit its title and body (5.4). Report and exit 0.

Exit codes: 0 for up-to-date, merged, or conflict-PR-updated (all are successful outcomes); non-zero only for infrastructure failure (fetch, push after retry, `gh` API), which GitHub surfaces as a failed run and emails the maintainer.

### 5.3 Merge commit message

```
Sync DeepWorlds dev: 13 commits (43a0a5f..5b7f4be)

Merges DeepWorldsSA/DeepWorlds_GMCAbilitySystem dev into dev.

Commits:
- 5b7f4be 2026-09-22 Eric Rajot Merge pull request #18 from Candescence/dev
- ...

Overlap with local changes since 43a0a5f:
- Source/GMCAbilitySystem/Private/Components/GMCAbilityComponent.cpp
```

"Overlap: none" when the intersection is empty. The subject's range is `<base7>..<tip7>`.

### 5.4 Conflict pull request

- Head `sync/deepworlds`, base `dev`, label `deepworlds-sync`. One PR at a time: later runs edit the same PR.
- Title: `Sync DeepWorlds dev: 13 commits, conflicts in 2 files`.
- Body:

```
Automated merge of DeepWorldsSA/DeepWorlds_GMCAbilitySystem `dev` (43a0a5f..5b7f4be) into `dev` failed.

## Conflicting files
- Source/GMCAbilitySystem/Private/Components/GMCAbilityComponent.cpp
- Source/GMCAbilitySystem/Public/Utility/GMASBoundQueueV2.h

## Overlap with local changes since 43a0a5f
- Source/GMCAbilitySystem/Private/Components/GMCAbilityComponent.cpp

## Commits
- 5b7f4be 2026-09-22 Eric Rajot Merge pull request #18 from Candescence/dev
- ...

## Resolve locally
    git fetch origin dev sync/deepworlds
    git checkout dev && git pull --ff-only
    git merge --no-ff origin/sync/deepworlds
    # resolve conflicts, build locally, then
    git push origin dev

The next scheduled run closes this PR and deletes `sync/deepworlds` once `dev` contains 5b7f4be.
```

Do not press "Merge" on GitHub for this PR: it would merge the unresolved fork tip. The PR exists to carry the report and the branch.

### 5.5 Edge cases

| Situation | Behavior |
|---|---|
| Fork unreachable, branch renamed or deleted | fetch fails → job fails → email. Fix `UPSTREAM_*` in the workflow. |
| Fork force-pushed (rewritten history) | merge-base moves; the merge still works; the commit list reflects the new history. |
| Maintainer pushed to `dev` during the run | push rejected → one re-fetch and retry → else fail. |
| Conflict PR open, then the maintainer resolves by hand and pushes `dev` | next run finds the fork tip in `dev` → closes PR, deletes branch. |
| Conflict PR open, fork moves further | branch force-updated to the new tip, PR title and body edited, conflict list recomputed. |
| Two runs at once (manual + cron) | `concurrency` queues the second. |
| `dry_run` | everything computed and reported; no push, no PR, tree restored. |

### 5.6 Security

- Token: `GITHUB_TOKEN` with `contents: write`, `pull-requests: write` and `issues: write` (labels live in the Issues API) for this job only; no stored secrets.
- Triggers: `schedule` and `workflow_dispatch` only. Never `pull_request`, so no code from a fork pull request runs with write access.
- Force-push is confined to `sync/deepworlds`, a branch nothing else uses.
- Fork code lands in `dev` without review by design; `dev` is documented as unstable and the `dev` → `main` promotion is the review and build point.
- Actions are pinned to major tags (`actions/checkout@v4`); the repo does not require SHA pinning.

## 6. Documentation deliverables

- `docs/BRANCHING.md`: the table in section 3; "I pushed a change" (build locally, push `dev`); "promote to `main`" (PR, local build of a downstream project, tag); "a sync PR is open" (the 5.4 recipe, and why not to press Merge); "run a sync now" (`workflow_dispatch`, `dry_run`); "consume GMAS as a submodule" (section 7).
- `README.md`: one line pointing to `docs/BRANCHING.md` and stating that `dev` is unstable and `main` is the release branch.

## 7. Consuming GMAS as a submodule

The plugin's internal name (`GMCAbilitySystem` in the `.uplugin`, module names, `.uproject` plugin references) is unchanged by any of this; a project that vendored a copy under another folder name only changes the folder.

Recipe for a project that vendors a copy (tracked in the project's repo or as a nested clone):
1. Remove the vendored folder (`git rm -r Plugins/<OldFolder>`, or delete the nested clone).
2. `git submodule add -b dev https://github.com/reznok/GMCAbilitySystem.git Plugins/GMCAbilitySystem` (or `-b main` for a project that wants releases only).
3. Regenerate project files, build.
4. From then on: fix upstream, push to `dev`, bump the submodule pointer in the project. Local patches to the submodule are never kept.

A project in the middle of a feature branch that carries vendored patches switches after that branch merges; its patches are upstreamed in step 3 of section 4 so the switch loses nothing.

## 8. Testing

The script is tested before the workflow ever runs on GitHub, in a research clone that has the fork and the downstream checkout as remotes (the downstream checkout is a known conflict fixture against the fork tip):

| # | Scenario | Setup | Expected |
|---|---|---|---|
| T1 | up to date | `TARGET_BRANCH` at or past the fork tip | report "up to date", exit 0, tree unchanged |
| T2 | clean merge, dry run | `dev` behind the fork, `DRY_RUN=true` | report with count and overlap; `git status` clean; `HEAD` unchanged |
| T3 | clean merge, live | temporary `origin` = a local bare clone; `DRY_RUN=false`; `gh` calls stubbed by a `gh` shim on `PATH` that logs its arguments | bare clone's `dev` advanced by one merge commit with the 5.3 message; shim log shows a PR close attempted only if a PR was reported open |
| T4 | conflict, dry run | `UPSTREAM_REMOTE`/`UPSTREAM_BRANCH` pointed at the conflict fixture | conflicting files listed, exit 0, tree clean |
| T5 | conflict, live | as T3 with the T4 refs | bare clone has `sync/deepworlds` = fixture tip; shim log shows `gh label create`, `gh pr list`, `gh pr create` with the 5.4 title and body |
| T6 | push rejected once | T3, plus a `pre-receive` hook on the bare clone that rejects the first push | second attempt succeeds; report mentions the retry |

Then on GitHub: `workflow_dispatch` with `dry_run=true` (expected: up to date, since reunification already merged the fork), then the first scheduled run.

## 9. Follow-ups (not in this spec)

- `release.yml`: on tag `v*`, zip the plugin (excluding `.github` and `docs`) as a release asset.
- Compile and spec CI on a self-hosted runner, if the maintainer ever wants it.
- Offering the maintainer's features back to the DeepWorlds fork.
- A Claude Code plugin for GMAS users, distributed from this repository.
