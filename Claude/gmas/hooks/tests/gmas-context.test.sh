#!/usr/bin/env bash
# Harness for hooks/gmas-context.sh: fixture project trees under a temp dir, one assertion set each,
# plus F9, the hooks.json command line run through sh. hooks/tests/gmas-context.test.ps1 is the
# PowerShell twin (same F1 to F8; its F9 runs the command line through PowerShell).
# Every assertion records ok or FAIL and continues; the final line is "N passed, M failed".
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HOOK="$HERE/../gmas-context.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); echo "ok   - $1"; }
fail() { FAIL=$((FAIL+1)); echo "FAIL - $1"; if [ -n "${2:-}" ]; then printf '%s\n' "$2" | sed 's/^/       /'; fi; return 0; }

py() { if command -v python3 >/dev/null 2>&1; then python3 "$@"; else python "$@"; fi; }

# run_hook <dir> [env assignments...]: runs the hook from <dir>; sets out (its stdout) and rc (its exit status).
out=""; rc=0
run_hook() { local d="$1"; shift; rc=0; out="$( (cd "$d" && env "$@" bash "$HOOK") )" || rc=$?; }

# assert_exit0 <name>: the hook must exit 0 on every path, including silence.
assert_exit0() { if [ "$rc" -eq 0 ]; then ok "$1: exit 0"; else fail "$1: exit 0" "exit $rc${out:+; output: $out}"; fi; }

# context <name> <json>: sets ctx to additionalContext; unparsable output records a failure and leaves ctx empty.
ctx=""
context() {
	if ctx="$(printf '%s' "$2" | py -c 'import json,sys; print(json.load(sys.stdin)["hookSpecificOutput"]["additionalContext"])' 2>&1)"; then return 0; fi
	fail "$1: JSON with additionalContext" "$(printf '%s\n' "$ctx" | tail -n 1)"$'\n'"output: $2"
	ctx=""
}

uplugin() { # uplugin <path> <VersionName>
	mkdir -p "$(dirname "$1")"
	printf '{\n\t"FileVersion": 3,\n\t"VersionName": "%s",\n\t"Modules": []\n}\n' "$2" > "$1"
}

assert_contains() { # assert_contains <name> <haystack> <needle>...
	local name="$1" hay="$2"; shift 2
	for n in "$@"; do
		if ! printf '%s' "$hay" | grep -qF -- "$n"; then fail "$name: contains '$n'" "$hay"; return 0; fi
	done
	ok "$name"
}

# F1: no project anywhere -> silence.
mkdir -p "$WORK/none/deep"
run_hook "$WORK/none/deep"
if [ -z "$out" ]; then ok "F1 no project: silent"; else fail "F1 no project: silent" "$out"; fi
assert_exit0 "F1 no project"

# F2: project without plugins -> silence.
mkdir -p "$WORK/p2/Source"; : > "$WORK/p2/Game.uproject"
run_hook "$WORK/p2/Source"
if [ -z "$out" ]; then ok "F2 project without GMAS: silent"; else fail "F2 project without GMAS: silent" "$out"; fi
assert_exit0 "F2 project without GMAS"

# F3: pre-1.4 GMAS copy under another folder name, no GMC.
mkdir -p "$WORK/p3"; : > "$WORK/p3/Game.uproject"
uplugin "$WORK/p3/Plugins/DeepWorlds_GMCAbilitySystem/GMCAbilitySystem.uplugin" "1.3"
run_hook "$WORK/p3"
context "F3 pre-1.4 GMAS without GMC" "$out"
assert_contains "F3 pre-1.4 GMAS without GMC" "$ctx" \
	'GMAS (GMC Ability System) at `Plugins/DeepWorlds_GMCAbilitySystem` (VersionName 1.3, pre-1.4' \
	'GMC was not found under `Plugins/`' \
	'gmas:gmas-upgrade'
assert_exit0 "F3 pre-1.4 GMAS without GMC"

# F4: GMC + 1.4 GMAS in a submodule-named folder, session started in a subdirectory.
mkdir -p "$WORK/p4/Source/Game"; : > "$WORK/p4/Game.uproject"
uplugin "$WORK/p4/Plugins/GMC/GMC.uplugin" "2.3.9"
uplugin "$WORK/p4/Plugins/GMCAbilitySystem/GMCAbilitySystem.uplugin" "1.4"
mkdir -p "$WORK/p4/Plugins/GMCAbilitySystem/Source/GMCAbilitySystem/Public/Utility"
: > "$WORK/p4/Plugins/GMCAbilitySystem/Source/GMCAbilitySystem/Public/Utility/GMASBoundQueueV2.h"
run_hook "$WORK/p4/Source/Game"
context "F4 GMC + GMAS 1.4 from a subdirectory" "$out"
assert_contains "F4 GMC + GMAS 1.4 from a subdirectory" "$ctx" \
	'GMC (General Movement Component) at `Plugins/GMC` (2.3.9)' \
	'GMAS (GMC Ability System) at `Plugins/GMCAbilitySystem` (VersionName 1.4, 1.4+ bound queue V2)' \
	'`Plugins/GMC/Source/GMCCore/Public/' \
	'never copy GMC code' \
	'gmas:gmas-rules' 'gmas:gmc-prediction' 'gmas:gmas-ability' 'gmas:gmas-effect' 'gmas:gmas-attribute' \
	'gmas:gmas-task' 'gmas:gmas-debug' 'gmas:gmas-review' 'gmas:gmas-testing' 'gmas:gmas-setup' 'gmas:gmas-upgrade'
assert_exit0 "F4 GMC + GMAS 1.4 from a subdirectory"

# F5: a session inside a GMAS submodule of a project is the project's session, not the maintainer's.
run_hook "$WORK/p4/Plugins/GMCAbilitySystem/Source"
context "F5 inside the submodule" "$out"
assert_contains "F5 inside the submodule: project context" "$ctx" '`Plugins/GMCAbilitySystem` (VersionName 1.4'
if printf '%s' "$ctx" | grep -q 'gmas-maintain'; then fail "F5 inside the submodule: no maintainer context" "$ctx"; else ok "F5 inside the submodule: no maintainer context"; fi
assert_exit0 "F5 inside the submodule"

# F6: a standalone checkout of the GMAS repository -> maintainer context.
mkdir -p "$WORK/repo/Source"; : > "$WORK/repo/GMCAbilitySystem.uplugin"
run_hook "$WORK/repo/Source"
context "F6 GMAS repository checkout" "$out"
assert_contains "F6 GMAS repository checkout" "$ctx" 'GMAS plugin repository' 'gmas:gmas-maintain' 'generic'
assert_exit0 "F6 GMAS repository checkout"

# F7: disabled by environment -> silence.
run_hook "$WORK/p4" CLAUDE_GMAS_HOOK_DISABLE=1
if [ -z "$out" ]; then ok "F7 disabled: silent"; else fail "F7 disabled: silent" "$out"; fi
assert_exit0 "F7 disabled"

# F8: output is a single line of valid JSON with the expected event name.
run_hook "$WORK/p4"
if [ "$(printf '%s\n' "$out" | wc -l)" -eq 1 ]; then ok "F8 one line"; else fail "F8 one line" "$out"; fi
ev="$(printf '%s' "$out" | py -c 'import json,sys; print(json.load(sys.stdin)["hookSpecificOutput"]["hookEventName"])' 2>/dev/null)" || ev="(unparsable JSON)"
if [ "$ev" = "SessionStart" ]; then ok "F8 hookEventName"; else fail "F8 hookEventName" "$ev"; fi
assert_exit0 "F8 project with GMC and GMAS"

# F9: the hooks.json command as Claude Code runs it with the bash shell (sh -c on macOS and Linux,
# Git Bash on Windows): exactly one JSON line (the PowerShell part must not run), nothing on stderr.
cmd="$(py -c 'import json,sys; print(json.load(open(sys.argv[1]))["hooks"]["SessionStart"][0]["hooks"][0]["command"])' "$HERE/../hooks.json")"
plugin_root="$(cd "$HERE/../.." && pwd)"
for case in "project|$WORK/p4/Source/Game|\`Plugins/GMCAbilitySystem\` (VersionName 1.4" "repository|$WORK/repo|gmas:gmas-maintain"; do
	name="F9 hooks.json under sh, ${case%%|*}"; rest="${case#*|}"; d="${rest%%|*}"; needle="${rest#*|}"
	rc=0; out="$( (cd "$d" && env -u CLAUDE_GMAS_HOOK_DISABLE -u CLAUDE_GMAS_HOOK_DEBUG CLAUDE_PLUGIN_ROOT="$plugin_root" sh -c "$cmd" 2>"$WORK/f9.err") )" || rc=$?
	if [ -n "$out" ] && [ "$(printf '%s
' "$out" | wc -l)" -eq 1 ]; then ok "$name: one line"; else fail "$name: one line" "$out"; fi
	context "$name" "$out"
	assert_contains "$name: context" "$ctx" "$needle"
	if [ ! -s "$WORK/f9.err" ]; then ok "$name: no stderr"; else fail "$name: no stderr" "$(cat "$WORK/f9.err")"; fi
	assert_exit0 "$name"
done

echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
