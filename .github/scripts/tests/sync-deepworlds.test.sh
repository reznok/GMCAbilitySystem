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
d=$(make_fixture t1)
before=$(git -C "$d/work" rev-parse HEAD)
out=$(run_sync "$d")
assert_contains "$out" "up to date" "T1 reports up to date"
assert_eq "$(git -C "$d/work" rev-parse HEAD)" "$before" "T1 HEAD unchanged"
assert_contains "$(cat "$GH_LOG")" "pr list" "T1 looks for a stale conflict PR"

printf '\n%s passed, %s failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
