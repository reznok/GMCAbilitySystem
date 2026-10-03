#!/usr/bin/env bash
# Tests for sync-deepworlds.sh. Builds throwaway repositories under a temp dir
# and stubs `gh`; no network, no GitHub.
# Run: bash .github/scripts/tests/sync-deepworlds.test.sh
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
SCRIPT="$HERE/../sync-deepworlds.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.com
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.com
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
export GH_TOKEN=fake
unset GITHUB_STEP_SUMMARY
export GH_LOG="$TMP/gh.log"
export GH_PR_LIST_JSON="[]"

# gh shim: logs every call, answers `pr list` from GH_PR_LIST_JSON.
mkdir -p "$TMP/bin"
cat >"$TMP/bin/gh" <<'SHIM'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$GH_LOG"
case "${1:-} ${2:-}" in
	"pr list") printf '%s\n' "$GH_PR_LIST_JSON" ;;
	"pr create") echo "https://example.invalid/pull/1" ;;
esac
SHIM
chmod +x "$TMP/bin/gh"
export PATH="$TMP/bin:$PATH"

pass=0
fail=0
ok() { pass=$((pass + 1)); printf 'ok   %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL %s\n' "$1"; }
assert_contains() { # haystack needle label
	if printf '%s' "$1" | grep -qF -- "$2"; then ok "$3"; else bad "$3 (missing: $2)"; printf '%s\n' "$1" | sed 's/^/     | /'; fi
}
assert_not_contains() { # haystack needle label
	if printf '%s' "$1" | grep -qF -- "$2"; then bad "$3 (unexpected: $2)"; else ok "$3"; fi
}
assert_eq() { # got want label
	if [ "$1" = "$2" ]; then ok "$3"; else bad "$3 (got '$1', want '$2')"; fi
}

# A fixture: canonical bare repo (origin), fork bare repo, a work clone of the
# canonical dev branch, and a fork clone to make fork commits in.
# shared.txt has 20 numbered lines so both sides can edit different lines.
make_fixture() { # name -> path
	local d="$TMP/$1"
	mkdir -p "$d"
	git init -q --bare -b dev "$d/canonical.git"
	git init -q -b dev "$d/seed"
	(
		cd "$d/seed" &&
			seq 1 20 | sed 's/^/line /' >shared.txt && echo base >file.txt &&
			git add . && git commit -q -m base && git push -q "$d/canonical.git" dev
	)
	git clone -q --bare "$d/canonical.git" "$d/fork.git"
	git clone -q -b dev "$d/canonical.git" "$d/work"
	git clone -q -b dev "$d/fork.git" "$d/forkwork"
	printf '%s' "$d"
}
fork_edit() { # fixture file sed-expr message
	(cd "$1/forkwork" && sed -i "$3" "$2" && git commit -qam "$4" && git push -q origin dev)
}
local_edit() { # fixture file sed-expr message (committed in work, pushed to canonical)
	(cd "$1/work" && sed -i "$3" "$2" && git commit -qam "$4" && git push -q origin dev)
}
run_sync() { # fixture [VAR=value ...] -> stdout+stderr; never fails the harness
	local d="$1"
	shift
	(cd "$d/work" && env UPSTREAM_URL="$d/fork.git" "$@" bash "$SCRIPT" 2>&1) || printf '\n[exit %s]' "$?"
}
sync_branch_sha() { git -C "$1/canonical.git" rev-parse --verify -q refs/heads/sync/deepworlds || true; }

# ---- T1: up to date
: >"$GH_LOG"
d=$(make_fixture t1)
before=$(git -C "$d/work" rev-parse HEAD)
out=$(run_sync "$d")
assert_contains "$out" "up to date" "T1 reports up to date"
assert_eq "$(git -C "$d/work" rev-parse HEAD)" "$before" "T1 HEAD unchanged"
assert_contains "$(cat "$GH_LOG")" "pr list" "T1 looks for a stale conflict PR"

# ---- T2: clean merge, dry run (fork edits line 20 and file.txt, local edits line 1: overlap, no conflict)
: >"$GH_LOG"
d=$(make_fixture t2)
local_edit "$d" shared.txt 's/^line 1$/line 1 local/' "local: line 1"
fork_edit "$d" shared.txt 's/^line 20$/line 20 fork/' "fork: line 20"
fork_edit "$d" file.txt 's/^base$/base fork/' "fork: file"
before=$(git -C "$d/work" rev-parse HEAD)
out=$(run_sync "$d" DRY_RUN=true)
assert_contains "$out" "would merge 2 commits" "T2 dry run counts fork commits"
assert_contains "$out" "- shared.txt" "T2 overlap lists shared.txt"
assert_not_contains "$out" "- file.txt" "T2 overlap excludes the fork-only file"
assert_eq "$(git -C "$d/work" rev-parse HEAD)" "$before" "T2 HEAD unchanged after dry run"
assert_eq "$(git -C "$d/work" status --porcelain)" "" "T2 tree clean after dry run"

# ---- T3: clean merge, live
: >"$GH_LOG"
out=$(run_sync "$d")
assert_contains "$out" "merged 2 commits" "T3 reports the merge"
assert_contains "$(git -C "$d/canonical.git" log -1 --format=%s dev)" "Sync DeepWorlds dev: 2 commits (" "T3 merge commit pushed to canonical dev"
body=$(git -C "$d/canonical.git" log -1 --format=%b dev)
assert_contains "$body" "Overlap with local changes since" "T3 merge body has the overlap section"
assert_contains "$body" "- shared.txt" "T3 merge body lists the overlap"
assert_contains "$body" "fork: line 20" "T3 merge body lists fork commits"
assert_not_contains "$(cat "$GH_LOG")" "pr create" "T3 opens no PR"
assert_eq "$(sync_branch_sha "$d")" "" "T3 leaves no sync branch"

# ---- T4: conflict, dry run (both sides edit line 1)
: >"$GH_LOG"
d=$(make_fixture t4)
local_edit "$d" shared.txt 's/^line 1$/line 1 local/' "local: line 1"
fork_edit "$d" shared.txt 's/^line 1$/line 1 fork/' "fork: line 1"
out=$(run_sync "$d" DRY_RUN=true)
assert_contains "$out" "conflict in 1 files" "T4 dry run reports the conflict"
assert_contains "$out" "- shared.txt" "T4 lists the conflicting file"
assert_eq "$(git -C "$d/work" status --porcelain)" "" "T4 tree clean after abort"
assert_eq "$(sync_branch_sha "$d")" "" "T4 dry run pushes no sync branch"

# ---- T5a: conflict, live: branch pushed, PR created
: >"$GH_LOG"
out=$(run_sync "$d")
assert_contains "$out" "conflicts in 1 files; pull request created" "T5a reports the PR"
forktip=$(git -C "$d/fork.git" rev-parse dev)
assert_eq "$(sync_branch_sha "$d")" "$forktip" "T5a sync branch equals the fork tip"
ghlog=$(cat "$GH_LOG")
assert_contains "$ghlog" "label create deepworlds-sync" "T5a ensures the label"
assert_contains "$ghlog" "pr create --head sync/deepworlds --base dev --title Sync DeepWorlds dev: 1 commits, conflicts in 1 files" "T5a creates the PR with the title"
assert_contains "$ghlog" "## Conflicting files" "T5a PR body has the conflict section"
assert_contains "$ghlog" "git merge --no-ff origin/sync/deepworlds" "T5a PR body has the resolve recipe"

# ---- T5b: conflict persists and a PR exists: edited, not created
: >"$GH_LOG"
out=$(run_sync "$d" GH_PR_LIST_JSON='[{"number":7}]')
ghlog=$(cat "$GH_LOG")
assert_contains "$ghlog" "pr edit 7 --title" "T5b edits the existing PR"
assert_not_contains "$ghlog" "pr create" "T5b creates no second PR"
assert_contains "$out" "pull request updated" "T5b reports the update"

# ---- T5c: resolved by hand and pushed: the next run closes the PR and deletes the branch
(cd "$d/work" && git merge -q --no-ff -X ours -m "resolve" "$forktip" && git push -q origin dev)
: >"$GH_LOG"
out=$(run_sync "$d" GH_PR_LIST_JSON='[{"number":7}]')
assert_contains "$out" "up to date" "T5c up to date after the manual resolve"
assert_contains "$(cat "$GH_LOG")" "pr close 7 --comment" "T5c closes the PR"
assert_eq "$(sync_branch_sha "$d")" "" "T5c deletes the sync branch"

# ---- T6: push rejected once (a pre-receive hook rejects the first push after arming);
# a stale conflict PR and sync branch exist and must be cleaned up after the clean merge
: >"$GH_LOG"
d=$(make_fixture t6)
fork_edit "$d" file.txt 's/^base$/base fork/' "fork: file"
git -C "$d/work" push -q origin "HEAD:refs/heads/sync/deepworlds"
cat >"$d/canonical.git/hooks/pre-receive" <<'HOOK'
#!/usr/bin/env bash
if [ -f "$GIT_DIR/reject-once" ]; then rm -f "$GIT_DIR/reject-once"; echo "rejected once" >&2; exit 1; fi
exit 0
HOOK
chmod +x "$d/canonical.git/hooks/pre-receive"
touch "$d/canonical.git/reject-once"
out=$(run_sync "$d" GH_PR_LIST_JSON='[{"number":9}]')
assert_contains "$out" "push rejected; refetching" "T6 logs the retry"
assert_contains "$out" "merged 1 commits" "T6 merges after the retry"
assert_contains "$out" "attempt 2" "T6 report mentions the second attempt"
assert_contains "$(git -C "$d/canonical.git" log -1 --format=%s dev)" "Sync DeepWorlds dev: 1 commits" "T6 canonical dev advanced"
assert_contains "$(cat "$GH_LOG")" "pr close 9 --comment" "T6 closes the stale PR after the clean merge"
assert_eq "$(sync_branch_sha "$d")" "" "T6 deletes the stale sync branch"

# ---- T7: a real non-fast-forward race: canonical dev moves after work was cloned,
# so the first push is rejected; the retry refetches dev and merges from the new tip
: >"$GH_LOG"
d=$(make_fixture t7)
fork_edit "$d" file.txt 's/^base$/base fork/' "fork: file"
git clone -q -b dev "$d/canonical.git" "$d/other"
(cd "$d/other" && sed -i 's/^line 10$/line 10 other/' shared.txt && git commit -qam "other: line 10" && git push -q origin dev)
moved=$(git -C "$d/canonical.git" rev-parse dev)
out=$(run_sync "$d")
assert_contains "$out" "push rejected; refetching" "T7 first push rejected (non-fast-forward)"
assert_contains "$out" "attempt 2" "T7 merged on the second attempt"
assert_eq "$(git -C "$d/canonical.git" merge-base --is-ancestor "$moved" dev && echo yes)" "yes" "T7 canonical dev keeps the concurrent commit"
assert_contains "$(git -C "$d/canonical.git" log -1 --format=%s dev)" "Sync DeepWorlds dev: 1 commits" "T7 canonical dev ends on the sync merge"

printf '\n%s passed, %s failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
