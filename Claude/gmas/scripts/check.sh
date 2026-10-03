#!/usr/bin/env bash
# Structural checks for the gmas Claude Code plugin. Usage: check.sh [repo-root]
# Prints one line per check; final line "N passed, M failed"; exit 1 when anything failed.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${1:-$(cd "$HERE/../../.." && pwd)}"
PLUGIN="$ROOT/Claude/gmas"
MARKET="$ROOT/.claude-plugin/marketplace.json"

EXPECTED_SKILLS="gmc-prediction gmas-rules gmas-setup gmas-ability gmas-effect gmas-attribute gmas-task gmas-debug gmas-review gmas-testing gmas-upgrade gmas-maintain"
MAX_LINES=300
# Downstream names that must never appear (kept here, not in the skills).
LEAK_WORDS='Iliad|Seek|Cynosure|OmegaShooters|MCPTesting'
# Drive paths like D:/ or C:\ (but not the "s:/" inside https://) and home directories.
LEAK_PATHS='(^|[^A-Za-z])[A-Za-z]:[\\/]|/Users/|/home/[a-z]'

PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); echo "ok   - $1"; }
fail() { FAIL=$((FAIL+1)); echo "FAIL - $1"; [ -n "${2:-}" ] && printf '%s\n' "$2" | sed 's/^/       /'; }

py() { if command -v python3 >/dev/null 2>&1; then python3 "$@"; else python "$@"; fi; }

# 1. Manifests validate (strict).
if command -v claude >/dev/null 2>&1; then
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

# 6. Leak grep over the plugin and the marketplace. The two checker scripts carry the patterns
#    and the harness's injected violations, so they are the only files excluded.
EXCL=(--exclude=check.sh --exclude=check.test.sh)
hits="$(grep -rnE "${EXCL[@]}" "$LEAK_WORDS" "$PLUGIN" "$MARKET" 2>/dev/null; grep -rnE "${EXCL[@]}" "$LEAK_PATHS" "$PLUGIN" "$MARKET" 2>/dev/null)"
[ -z "$hits" ] && ok "leak grep clean" || fail "leak grep clean (leak)" "$hits"

# 7. GMC excerpt guard: no GRIMTEC/author copyright lines; no fenced block defining a GMC symbol.
hits="$(grep -rnE "${EXCL[@]}" 'GRIMTEC|Dominik Lips|Copyright.*GMC' "$PLUGIN" 2>/dev/null)"
[ -z "$hits" ] && ok "no GMC copyright lines" || fail "no GMC copyright lines (GMC excerpt)" "$hits"
hits="$(find "$PLUGIN" -name '*.md' -print0 | xargs -0 awk '
	/^```/ { infence = !infence; next }
	infence && /(GMCCORE_API|UGMC_ReplicationCmp::|UGMC_MovementUtilityCmp::|UGMC_OrganicMovementCmp::)/ { print FILENAME ":" FNR ": " $0 }')"
[ -z "$hits" ] && ok "no GMC definitions in code blocks" || fail "no GMC definitions in code blocks (GMC excerpt)" "$hits"

# 8. Hook harness (when present).
if [ -f "$PLUGIN/hooks/tests/gmas-context.test.sh" ]; then
	if out="$(bash "$PLUGIN/hooks/tests/gmas-context.test.sh" 2>&1)"; then ok "hook harness: $(printf '%s\n' "$out" | tail -n 1)"; else fail "hook harness" "$out"; fi
fi

echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
