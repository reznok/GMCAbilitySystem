#!/usr/bin/env bash
# Harness for hooks/gmas-context.sh: fixture project trees under a temp dir, one assertion set each.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HOOK="$HERE/../gmas-context.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); echo "ok   - $1"; }
fail() { FAIL=$((FAIL+1)); echo "FAIL - $1"; [ -n "${2:-}" ] && printf '%s\n' "$2" | sed 's/^/       /'; }

py() { if command -v python3 >/dev/null 2>&1; then python3 "$@"; else python "$@"; fi; }

# run_hook <dir> [env assignments...]: prints the hook's stdout, run from <dir>.
run_hook() { local d="$1"; shift; (cd "$d" && env "$@" bash "$HOOK"); }

# context <json>: extracts additionalContext, or fails the JSON check.
context() { printf '%s' "$1" | py -c 'import json,sys; print(json.load(sys.stdin)["hookSpecificOutput"]["additionalContext"])'; }

uplugin() { # uplugin <path> <VersionName>
	mkdir -p "$(dirname "$1")"
	printf '{\n\t"FileVersion": 3,\n\t"VersionName": "%s",\n\t"Modules": []\n}\n' "$2" > "$1"
}

assert_contains() { # assert_contains <name> <haystack> <needle>...
	local name="$1" hay="$2"; shift 2
	for n in "$@"; do
		if ! printf '%s' "$hay" | grep -qF -- "$n"; then fail "$name: contains '$n'" "$hay"; return; fi
	done
	ok "$name"
}

# F1: no project anywhere -> silence.
mkdir -p "$WORK/none/deep"
out="$(run_hook "$WORK/none/deep")"
[ -z "$out" ] && ok "F1 no project: silent" || fail "F1 no project: silent" "$out"

# F2: project without plugins -> silence.
mkdir -p "$WORK/p2/Source"; : > "$WORK/p2/Game.uproject"
out="$(run_hook "$WORK/p2/Source")"
[ -z "$out" ] && ok "F2 project without GMAS: silent" || fail "F2 project without GMAS: silent" "$out"

# F3: pre-1.4 GMAS copy under another folder name, no GMC.
mkdir -p "$WORK/p3"; : > "$WORK/p3/Game.uproject"
uplugin "$WORK/p3/Plugins/DeepWorlds_GMCAbilitySystem/GMCAbilitySystem.uplugin" "1.3"
out="$(run_hook "$WORK/p3")"
ctx="$(context "$out")"
assert_contains "F3 pre-1.4 GMAS without GMC" "$ctx" \
	'GMAS (GMC Ability System) at `Plugins/DeepWorlds_GMCAbilitySystem` (1.3, pre-1.4' \
	'GMC was not found under `Plugins/`' \
	'gmas:gmas-upgrade'

# F4: GMC + 1.4 GMAS in a submodule-named folder, session started in a subdirectory.
mkdir -p "$WORK/p4/Source/Game"; : > "$WORK/p4/Game.uproject"
uplugin "$WORK/p4/Plugins/GMC/GMC.uplugin" "2.3.9"
uplugin "$WORK/p4/Plugins/GMCAbilitySystem/GMCAbilitySystem.uplugin" "1.4"
mkdir -p "$WORK/p4/Plugins/GMCAbilitySystem/Source/GMCAbilitySystem/Public/Utility"
: > "$WORK/p4/Plugins/GMCAbilitySystem/Source/GMCAbilitySystem/Public/Utility/GMASBoundQueueV2.h"
out="$(run_hook "$WORK/p4/Source/Game")"
ctx="$(context "$out")"
assert_contains "F4 GMC + GMAS 1.4 from a subdirectory" "$ctx" \
	'GMC (General Movement Component) at `Plugins/GMC` (2.3.9)' \
	'GMAS (GMC Ability System) at `Plugins/GMCAbilitySystem` (1.4, 1.4+ bound queue V2)' \
	'`Plugins/GMC/Source/GMCCore/Public/' \
	'never copy GMC code' \
	'gmas:gmas-rules' 'gmas:gmc-prediction' 'gmas:gmas-ability' 'gmas:gmas-effect' 'gmas:gmas-attribute' \
	'gmas:gmas-task' 'gmas:gmas-debug' 'gmas:gmas-review' 'gmas:gmas-testing' 'gmas:gmas-setup' 'gmas:gmas-upgrade'

# F5: a session inside a GMAS submodule of a project is the project's session, not the maintainer's.
out="$(run_hook "$WORK/p4/Plugins/GMCAbilitySystem/Source")"
ctx="$(context "$out")"
assert_contains "F5 inside the submodule: project context" "$ctx" '`Plugins/GMCAbilitySystem` (1.4'
if printf '%s' "$ctx" | grep -q 'gmas-maintain'; then fail "F5 inside the submodule: no maintainer context"; else ok "F5 inside the submodule: no maintainer context"; fi

# F6: a standalone checkout of the GMAS repository -> maintainer context.
mkdir -p "$WORK/repo/Source"; : > "$WORK/repo/GMCAbilitySystem.uplugin"
out="$(run_hook "$WORK/repo/Source")"
ctx="$(context "$out")"
assert_contains "F6 GMAS repository checkout" "$ctx" 'GMAS plugin repository' 'gmas:gmas-maintain' 'generic'

# F7: disabled by environment -> silence.
out="$(run_hook "$WORK/p4" CLAUDE_GMAS_HOOK_DISABLE=1)"
[ -z "$out" ] && ok "F7 disabled: silent" || fail "F7 disabled: silent" "$out"

# F8: output is a single line of valid JSON with the expected event name.
out="$(run_hook "$WORK/p4")"
[ "$(printf '%s\n' "$out" | wc -l)" -eq 1 ] && ok "F8 one line" || fail "F8 one line" "$out"
ev="$(printf '%s' "$out" | py -c 'import json,sys; print(json.load(sys.stdin)["hookSpecificOutput"]["hookEventName"])')"
[ "$ev" = "SessionStart" ] && ok "F8 hookEventName" || fail "F8 hookEventName" "$ev"

echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
