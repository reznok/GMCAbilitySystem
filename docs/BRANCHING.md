# Branches

| Branch | Role | Who writes |
|---|---|---|
| `dev` | Unstable integration. Receives automated merges from the DeepWorlds fork and the maintainer's own pushes. May not compile on a given day. | sync job, maintainer |
| `main` | Stable. Promoted from `dev` by the maintainer after a local build. Releases are tags on `main` (`vX.Y.Z`). | maintainer |
| `sync/deepworlds` | Automation-owned. Exists only while a conflict pull request is open; always equals the fork's `dev` tip. | sync job |

GMAS has no compile CI: it depends on the paid GMC plugin, so builds happen on the maintainer's machine.

## The DeepWorlds sync

`.github/workflows/sync-deepworlds.yml` runs daily (06:00 UTC) and on demand. It fetches `DeepWorldsSA/DeepWorlds_GMCAbilitySystem` `dev` and merges it into `dev`:

- **Clean merge:** pushed to `dev`. The merge commit lists the fork commits and the files both sides touched since the last sync ("overlap"), which is the heads-up that fork work landed near local work.
- **Conflict:** the fork tip is pushed to `sync/deepworlds` and one pull request (label `deepworlds-sync`) lists the conflicting files and the commits. **Do not merge that pull request on GitHub**; it carries the unresolved fork tip. Resolve locally:

      git fetch origin dev sync/deepworlds
      git checkout dev
      git pull --ff-only
      git merge --no-ff origin/sync/deepworlds
      # resolve conflicts, build locally, then
      git push origin dev

  The next run sees that `dev` contains the fork tip, closes the pull request and deletes the branch.
- **Up to date:** nothing is merged; a stale conflict pull request is closed and `sync/deepworlds` deleted if `dev` already contains the fork tip.

Run it by hand from the Actions tab (`Run workflow`; tick `dry_run` to see what would happen without pushing), or locally in a full clone of `dev`. PowerShell (Windows; `sync-deepworlds.ps1` is a port of the bash script with the same behaviour):

    $env:DRY_RUN = 'true'; powershell -NoProfile -ExecutionPolicy Bypass -File .github/scripts/sync-deepworlds.ps1; Remove-Item Env:DRY_RUN

bash (macOS, Linux, Git Bash; the workflow runs this one):

    DRY_RUN=true bash .github/scripts/sync-deepworlds.sh

Their harnesses build throwaway repositories and stub `gh`: `powershell -NoProfile -ExecutionPolicy Bypass -File .github/scripts/tests/sync-deepworlds.test.ps1` and `bash .github/scripts/tests/sync-deepworlds.test.sh`.

Fork commits are merged, never cherry-picked, so later merges stay small. To drop an unwanted fork change, `git revert` it on `dev`.

## Pushing your own change

Build locally, then push to `dev`. If a sync lands at the same moment the job retries once; if your push is rejected, pull and push again.

## Promoting to `main`

1. Build at least one downstream project against `dev` and run the GMAS specs headless:
   `UnrealEditor-Cmd.exe <Project>.uproject -ExecCmds="Automation RunTests GMAS;Quit" -unattended -nullrhi -log`
2. Run the plugin check (expect `N passed, 0 failed`): `powershell -NoProfile -ExecutionPolicy Bypass -File Claude/gmas/scripts/check.ps1` in PowerShell, or `bash Claude/gmas/scripts/check.sh`, and bump `version` in `Claude/gmas/.claude-plugin/plugin.json`: at least a patch bump whenever anything under `Claude/` or `.claude-plugin/` changed, `X.Y.0` when GMAS's minor version changes (installed plugins update only when it changes).
3. Bump `VersionName` in `GMCAbilitySystem.uplugin` on `dev` and push.
4. Promote with a merge commit that carries `dev`'s tree exactly (`main`'s 1.3 history is a squash, so a plain merge conflicts). PowerShell:

       git fetch origin
       $NEW = git commit-tree 'origin/dev^{tree}' -p origin/main -p origin/dev -m "Release X.Y: promote dev to main"
       git push origin "${NEW}:main"

   bash:

       git fetch origin
       NEW=$(git commit-tree origin/dev^{tree} -p origin/main -p origin/dev -m "Release X.Y: promote dev to main")
       git push origin "$NEW":main

5. Tag it and publish the release notes: `git tag -a vX.Y.Z $NEW -m "GMAS X.Y"` (bash: `"$NEW"`), then `git push origin vX.Y.Z` and `gh release create vX.Y.Z --notes-file ../gmas-release-notes.md --title "GMAS X.Y"` (keep the notes file outside the clone; an untracked file makes the sync script's clean-tree check fail).

## Consuming GMAS as a submodule

    git submodule add -b dev https://github.com/reznok/GMCAbilitySystem.git Plugins/GMCAbilitySystem

Use `-b main` for releases only. The plugin's name (`GMCAbilitySystem`) does not depend on the folder name. Fix upstream, push to `dev`, then bump the submodule pointer in your project; never keep local patches inside the submodule.

## The Claude Code plugin

`Claude/gmas` (and the marketplace manifest at `.claude-plugin/marketplace.json`) ships with every promotion of `dev` to `main`; users install from `main`. Before pushing a change under `Claude/`, run the plugin check (`powershell -NoProfile -ExecutionPolicy Bypass -File Claude/gmas/scripts/check.ps1`, or `bash Claude/gmas/scripts/check.sh`) and bump `version` in `Claude/gmas/.claude-plugin/plugin.json` (installed plugins update only when it changes). A fix confined to `Claude/` or `.claude-plugin/` may be cherry-picked from `dev` onto `main` and pushed without a full promotion.
