# Branches

| Branch | Role | Who writes |
|---|---|---|
| `dev` | Unstable integration. Receives automated merges from the DeepWorlds fork and the maintainer's own pushes. May not compile on a given day. | sync job, maintainer |
| `main` | Stable. Promoted from `dev` by pull request after a local build. Releases are tags on `main` (`vX.Y.Z`). | maintainer |
| `sync/deepworlds` | Automation-owned. Exists only while a conflict pull request is open; always equals the fork's `dev` tip. | sync job |

GMAS has no compile CI: it depends on the paid GMC plugin, so builds happen on the maintainer's machine.

## The DeepWorlds sync

`.github/workflows/sync-deepworlds.yml` runs daily (06:00 UTC) and on demand. It fetches `DeepWorldsSA/DeepWorlds_GMCAbilitySystem` `dev` and merges it into `dev`:

- **Clean merge:** pushed to `dev`. The merge commit lists the fork commits and the files both sides touched since the last sync ("overlap"), which is the heads-up that fork work landed near local work.
- **Conflict:** the fork tip is pushed to `sync/deepworlds` and one pull request (label `deepworlds-sync`) lists the conflicting files and the commits. **Do not merge that pull request on GitHub**; it carries the unresolved fork tip. Resolve locally:

      git fetch origin dev sync/deepworlds
      git checkout dev && git pull --ff-only
      git merge --no-ff origin/sync/deepworlds
      # resolve conflicts, build locally, then
      git push origin dev

  The next run sees that `dev` contains the fork tip, closes the pull request and deletes the branch.
- **Up to date:** nothing happens.

Run it by hand from the Actions tab (`Run workflow`; tick `dry_run` to see what would happen without pushing), or locally in a full clone of `dev`:

    DRY_RUN=true bash .github/scripts/sync-deepworlds.sh

Fork commits are merged, never cherry-picked, so later merges stay small. To drop an unwanted fork change, `git revert` it on `dev`.

## Pushing your own change

Build locally, then push to `dev`. If a sync lands at the same moment the job retries once; if your push is rejected, pull and push again.

## Promoting to `main`

1. Build at least one downstream project against `dev` and run the GMAS specs headless:
   `UnrealEditor-Cmd.exe <Project>.uproject -ExecCmds="Automation RunTests GMAS;Quit" -unattended -nullrhi -log`
2. Open a pull request `dev` → `main`, merge it.
3. Tag the merge on `main`: `git tag vX.Y.Z && git push origin vX.Y.Z`.

## Consuming GMAS as a submodule

    git submodule add -b dev https://github.com/reznok/GMCAbilitySystem.git Plugins/GMCAbilitySystem

Use `-b main` for releases only. The plugin's name (`GMCAbilitySystem`) does not depend on the folder name. Fix upstream, push to `dev`, then bump the submodule pointer in your project; never keep local patches inside the submodule.
