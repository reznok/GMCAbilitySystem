#!/usr/bin/env bash
# Structural checks for the gmas Claude Code plugin. Usage: [GMAS_CHECK_FAST=1] check.sh [repo-root]
# check.ps1 is the PowerShell twin (same checks; it also runs this script's bash hook harness).
# Prints one line per check; final line "N passed, M failed" (", K skipped" appended when checks
# were skipped); exit 1 when anything failed. GMAS_CHECK_FAST=1 skips the slow checks (manifest
# validation, hook harness) and prints a "skip" line for each; the harness uses it for the
# doctored scenarios.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${1:-$(cd "$HERE/../../.." && pwd)}"
PLUGIN="$ROOT/Claude/gmas"
MARKET="$ROOT/.claude-plugin/marketplace.json"
FAST="${GMAS_CHECK_FAST:-0}"

EXPECTED_SKILLS="gmc-prediction gmas-rules gmas-setup gmas-ability gmas-effect gmas-attribute gmas-task gmas-debug gmas-review gmas-testing gmas-upgrade gmas-maintain"
MAX_LINES=300
# Downstream names that must never appear (kept here, not in the skills).
LEAK_WORDS='Iliad|Seek|Cynosure|OmegaShooters|MCPTesting'
# Drive paths like D:/ or C:\ (but not the "s:/" inside https://) and home directories.
LEAK_PATHS='(^|[^A-Za-z])[A-Za-z]:[\\/]|/Users/|/home/[a-z]'

PASS=0; FAIL=0; SKIP=0
ok()   { PASS=$((PASS+1)); echo "ok   - $1"; }
skip() { SKIP=$((SKIP+1)); echo "skip - $1"; }
fail() { FAIL=$((FAIL+1)); echo "FAIL - $1"; if [ -n "${2:-}" ]; then printf '%s\n' "$2" | sed 's/^/       /'; fi; }

py() { if command -v python3 >/dev/null 2>&1; then python3 "$@"; else python "$@"; fi; }

# 1. Manifests validate (strict). Skipped in fast mode.
if [ "$FAST" = "1" ]; then
	skip "manifests validate (GMAS_CHECK_FAST)"
elif command -v claude >/dev/null 2>&1; then
	if out="$(claude plugin validate --strict "$PLUGIN" 2>&1)"; then ok "plugin manifest validates"; else fail "plugin manifest validates" "$out"; fi
	if out="$(claude plugin validate --strict "$MARKET" 2>&1)"; then ok "marketplace manifest validates"; else fail "marketplace manifest validates" "$out"; fi
else
	fail "claude CLI available for 'plugin validate'" "install Claude Code"
fi

# 2. Expected skills present.
missing=""
for s in $EXPECTED_SKILLS; do [ -f "$PLUGIN/skills/$s/SKILL.md" ] || missing="$missing $s"; done
if [ -z "$missing" ]; then ok "all expected skills present"; else fail "all expected skills present (missing:$missing)"; fi

# 3. Frontmatter: name == dir, description starts with "Use when", block <= 1024 chars.
for f in "$PLUGIN"/skills/*/SKILL.md; do
	[ -e "$f" ] || continue
	dir="$(basename "$(dirname "$f")")"
	fm="$(awk 'NR==1 && $0!="---"{exit} NR>1 && $0=="---"{exit} NR>1{print}' "$f")"
	name="$(printf '%s\n' "$fm" | sed -n 's/^name:[[:space:]]*//p' | head -n 1)"
	desc="$(printf '%s\n' "$fm" | sed -n 's/^description:[[:space:]]*//p' | head -n 1)"
	[ "$name" = "$dir" ] && ok "$dir: frontmatter name equals directory" || fail "$dir: frontmatter name equals directory" "name='$name'"
	case "$desc" in "Use when"*) ok "$dir: description starts with 'Use when'";; *) fail "$dir: description starts with 'Use when'" "description='$desc'";; esac
	[ "${#fm}" -le 1024 ] && ok "$dir: frontmatter <= 1024 chars" || fail "$dir: frontmatter <= 1024 chars" "${#fm} chars"
	n="$(wc -l < "$f")"
	[ "$n" -le "$MAX_LINES" ] && ok "$dir: SKILL.md <= $MAX_LINES lines" || fail "$dir: SKILL.md <= $MAX_LINES lines ($n)"
done

# 4. Relative links resolve (markdown links not starting with a scheme or '#').
broken=""
while IFS= read -r f; do
	d="$(dirname "$f")"
	while IFS= read -r target; do
		t="${target%%#*}"; [ -z "$t" ] && continue
		case "$t" in http://*|https://*|mailto:*) continue;; esac
		[ -e "$d/$t" ] || broken="$broken
$f -> $target"
	done < <(grep -oE '\]\(([^)]+)\)' "$f" | sed 's/^](//; s/)$//')
done < <(find "$PLUGIN" -name '*.md' -print)
[ -z "$broken" ] && ok "relative links resolve" || fail "relative links resolve (broken link)" "$broken"

# 5. hooks.json parses (when present).
if [ -f "$PLUGIN/hooks/hooks.json" ]; then
	if py -c 'import json,sys; json.load(open(sys.argv[1]))' "$PLUGIN/hooks/hooks.json" 2>/dev/null; then ok "hooks.json parses"; else fail "hooks.json parses"; fi
fi

# 6. Leak grep over the plugin and the marketplace. The checker scripts (bash and PowerShell)
#    carry the patterns and the harnesses' injected violations, so they are the only files excluded.
EXCL=(--exclude=check.sh --exclude=check.test.sh --exclude=check.ps1 --exclude=check.test.ps1)
hits="$(grep -rnE "${EXCL[@]}" "$LEAK_WORDS" "$PLUGIN" "$MARKET" 2>/dev/null; grep -rnE "${EXCL[@]}" "$LEAK_PATHS" "$PLUGIN" "$MARKET" 2>/dev/null)"
[ -z "$hits" ] && ok "leak grep clean" || fail "leak grep clean (leak)" "$hits"

# 7. GMC excerpt guard: no GRIMTEC/author copyright lines; no fenced block defining a GMC symbol.
hits="$(grep -rnE "${EXCL[@]}" 'GRIMTEC|Dominik Lips|Copyright.*GMC' "$PLUGIN" 2>/dev/null)"
[ -z "$hits" ] && ok "no GMC copyright lines" || fail "no GMC copyright lines (GMC excerpt)" "$hits"
# A fence line (``` or ~~~, possibly indented inside a list item) toggles the in-fence state; the
# state resets at the start of every file so an unterminated fence cannot hide the files after it.
# The file list is sorted so the scan order (and the order of hits) is the same everywhere.
hits="$(find "$PLUGIN" -name '*.md' -print0 | LC_ALL=C sort -z | xargs -0 -r awk '
	FNR == 1 { infence = 0 }
	/^[[:space:]]*(```|~~~)/ { infence = !infence; next }
	infence && /(GMCCORE_API|UGMC_ReplicationCmp::|UGMC_MovementUtilityCmp::|UGMC_OrganicMovementCmp::)/ { print FILENAME ":" FNR ": " $0 }')"
[ -z "$hits" ] && ok "no GMC definitions in code blocks" || fail "no GMC definitions in code blocks (GMC excerpt)" "$hits"

# 8. Hook harness (when present). Skipped in fast mode. Runs with stdin closed and, where a
#    timeout command exists, under a 120 s limit so a hung harness cannot hang the check.
HOOK_HARNESS="$PLUGIN/hooks/tests/gmas-context.test.sh"
if [ -f "$HOOK_HARNESS" ]; then
	if [ "$FAST" = "1" ]; then
		skip "hook harness (GMAS_CHECK_FAST)"
	else
		run=(bash "$HOOK_HARNESS")
		if command -v timeout >/dev/null 2>&1; then run=(timeout 120 "${run[@]}"); fi
		if out="$("${run[@]}" 2>&1 </dev/null)"; then ok "hook harness: $(printf '%s\n' "$out" | tail -n 1)"; else rc=$?; fail "hook harness (exit $rc)" "$out"; fi
	fi
fi

summary="$PASS passed, $FAIL failed"
if [ "$SKIP" -gt 0 ]; then summary="$summary, $SKIP skipped"; fi
echo "$summary"
[ "$FAIL" -eq 0 ]
