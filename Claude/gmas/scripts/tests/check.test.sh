#!/usr/bin/env bash
# Harness for scripts/check.sh: copies the plugin into a temp tree, injects one violation per
# scenario and asserts that check.sh fails with the expected message (and passes when clean).
# S1 runs the full check once; every doctored scenario runs with GMAS_CHECK_FAST=1, which skips
# manifest validation and the hook harness (slow, and not what those scenarios test).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../../.." && pwd)"
CHECK="$REPO/Claude/gmas/scripts/check.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); echo "ok   - $1"; }
fail() { FAIL=$((FAIL+1)); echo "FAIL - $1"; }

# fresh_copy <name>: a copy of the plugin + marketplace under $WORK/<name>; prints the path.
fresh_copy() {
	local dst="$WORK/$1"
	mkdir -p "$dst/Claude" "$dst/.claude-plugin"
	cp -R "$REPO/Claude/gmas" "$dst/Claude/gmas"
	cp "$REPO/.claude-plugin/marketplace.json" "$dst/.claude-plugin/marketplace.json"
	echo "$dst"
}

# expect_pass <name> <root>
expect_pass() {
	if out="$(bash "$CHECK" "$2" 2>&1)"; then ok "$1"; else fail "$1"; echo "$out" | tail -n 5; fi
}
# expect_fail <name> <root> <substring>: check.sh must fail, and a FAIL line must mention <substring>.
expect_fail() {
	if out="$(bash "$CHECK" "$2" 2>&1)"; then
		fail "$1 (check passed, expected failure)"
	elif echo "$out" | grep '^FAIL' | grep -q -- "$3"; then ok "$1"
	else fail "$1 (failed for the wrong reason)"; echo "$out" | grep '^FAIL' | tail -n 8 || true; fi
}

# A minimal valid skill so scenarios can run before the real skills exist.
# $1 root, $2 skill name
stub_skill() {
	mkdir -p "$1/Claude/gmas/skills/$2"
	printf -- '---\nname: %s\ndescription: Use when testing the check script.\n---\n\n# %s\n\nBody.\n' "$2" "$2" \
		> "$1/Claude/gmas/skills/$2/SKILL.md"
}
all_stubs() {
	for s in gmc-prediction gmas-rules gmas-setup gmas-ability gmas-effect gmas-attribute gmas-task gmas-debug gmas-review gmas-testing gmas-upgrade gmas-maintain; do
		[ -f "$1/Claude/gmas/skills/$s/SKILL.md" ] || stub_skill "$1" "$s"
	done
}

# S1: the real tree (with stubs for skills not written yet) passes, on the full (slow) path.
# An inherited GMAS_CHECK_FAST would silently make it the fast path, so clear it first.
unset GMAS_CHECK_FAST
R="$(fresh_copy clean)"; all_stubs "$R"
expect_pass "S1 clean tree passes" "$R"

# Every scenario below doctors one file; the slow checks add nothing to them.
export GMAS_CHECK_FAST=1

# S2: a downstream project name fails the leak grep.
R="$(fresh_copy leak)"; all_stubs "$R"
echo "Seen in Cynosure once." >> "$R/Claude/gmas/skills/gmas-rules/SKILL.md"
expect_fail "S2 leak grep catches a project name" "$R" "leak"

# S3: a drive path fails the leak grep; a URL does not.
R="$(fresh_copy drive)"; all_stubs "$R"
echo 'See https://example.com/docs and D:/Work/Game.' >> "$R/Claude/gmas/skills/gmas-rules/SKILL.md"
expect_fail "S3 leak grep catches a drive path" "$R" "leak"
R="$(fresh_copy url)"; all_stubs "$R"
echo 'See https://example.com/docs only.' >> "$R/Claude/gmas/skills/gmas-rules/SKILL.md"
expect_pass "S3b a URL alone passes" "$R"

# S4: description must start with "Use when".
R="$(fresh_copy desc)"; all_stubs "$R"
sed -i 's/^description: Use when/description: Helps with/' "$R/Claude/gmas/skills/gmas-debug/SKILL.md"
expect_fail "S4 description trigger rule" "$R" "Use when"

# S5: name must equal the directory.
R="$(fresh_copy name)"; all_stubs "$R"
sed -i 's/^name: gmas-task$/name: gmas-tasks/' "$R/Claude/gmas/skills/gmas-task/SKILL.md"
expect_fail "S5 name equals directory" "$R" "name equals"

# S6: SKILL.md over 300 lines fails.
R="$(fresh_copy long)"; all_stubs "$R"
for i in $(seq 1 310); do echo "line $i" >> "$R/Claude/gmas/skills/gmas-effect/SKILL.md"; done
expect_fail "S6 line budget" "$R" "300"

# S7: a GMC copyright line fails the excerpt guard.
R="$(fresh_copy grim)"; all_stubs "$R"
echo "// Copyright GRIMTEC" >> "$R/Claude/gmas/skills/gmc-prediction/SKILL.md"
expect_fail "S7 GMC excerpt guard (copyright)" "$R" "copyright"

# S8: a fenced block defining a GMC symbol fails the excerpt guard.
R="$(fresh_copy excerpt)"; all_stubs "$R"
printf '\n```cpp\nvoid UGMC_ReplicationCmp::Foo()\n{\n}\n```\n' >> "$R/Claude/gmas/skills/gmc-prediction/SKILL.md"
expect_fail "S8 GMC excerpt guard (definition)" "$R" "definitions in code blocks"

# S8b: an unterminated fence in an earlier file must not hide a definition in a later one
# (check.sh scans the files in sorted order; gmas-ability sorts before gmc-prediction).
R="$(fresh_copy carry)"; all_stubs "$R"
printf '\n```\nunterminated fence\n' >> "$R/Claude/gmas/skills/gmas-ability/SKILL.md"
printf '\n```cpp\nvoid UGMC_ReplicationCmp::Foo()\n{\n}\n```\n' >> "$R/Claude/gmas/skills/gmc-prediction/SKILL.md"
expect_fail "S8b GMC excerpt guard (fence state resets per file)" "$R" "definitions in code blocks"

# S8c: a fence indented inside a list item still opens a code block.
R="$(fresh_copy indent)"; all_stubs "$R"
printf '\n- Example:\n\n  ```cpp\n  void UGMC_ReplicationCmp::Foo()\n  {\n  }\n  ```\n' >> "$R/Claude/gmas/skills/gmc-prediction/SKILL.md"
expect_fail "S8c GMC excerpt guard (indented fence)" "$R" "definitions in code blocks"

# S9: a broken relative link fails.
R="$(fresh_copy link)"; all_stubs "$R"
echo "See [missing](references/nope.md)." >> "$R/Claude/gmas/skills/gmas-setup/SKILL.md"
expect_fail "S9 relative links resolve" "$R" "link"

# S10: a missing expected skill fails.
R="$(fresh_copy missing)"; all_stubs "$R"
rm -rf "$R/Claude/gmas/skills/gmas-upgrade"
expect_fail "S10 expected skills present" "$R" "gmas-upgrade"

# S11: unparsable hooks.json fails (only once Task 3 adds the file; skip gracefully before).
R="$(fresh_copy hooks)"; all_stubs "$R"
if [ -f "$R/Claude/gmas/hooks/hooks.json" ]; then
	echo "{ not json" > "$R/Claude/gmas/hooks/hooks.json"
	expect_fail "S11 hooks.json parses" "$R" "hooks.json"
else
	ok "S11 skipped (no hooks.json yet)"
fi

echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
