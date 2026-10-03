#!/usr/bin/env bash
# Merge the DeepWorlds fork's dev branch into this repository's dev branch.
#
# Clean merge:  the merge commit is pushed to TARGET_BRANCH.
# Conflict:     the fork tip is force-pushed to SYNC_BRANCH and one pull request
#               (created or updated) carries the report for a human to resolve.
# Up to date:   nothing changes; a stale conflict PR is closed.
#
# Runs from .github/workflows/sync-deepworlds.yml and from any full clone by
# hand (DRY_RUN=true needs no token). See docs/BRANCHING.md.
set -euo pipefail

UPSTREAM_URL="${UPSTREAM_URL:-https://github.com/DeepWorldsSA/DeepWorlds_GMCAbilitySystem.git}"
UPSTREAM_BRANCH="${UPSTREAM_BRANCH:-dev}"
UPSTREAM_REMOTE="${UPSTREAM_REMOTE:-deepworlds}"
TARGET_BRANCH="${TARGET_BRANCH:-dev}"
SYNC_BRANCH="${SYNC_BRANCH:-sync/deepworlds}"
DRY_RUN="${DRY_RUN:-false}"
LABEL="deepworlds-sync"
MAX_LISTED_COMMITS=50

log() { printf '%s\n' "$*" >&2; }
die() { log "error: $*"; exit 1; }
is_dry() { [ "$DRY_RUN" = "true" ]; }
short() { git rev-parse --short "$1"; }

# Markdown report: the Actions step summary when available, else stdout.
report() {
	if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
		printf '%s\n' "$*" >>"$GITHUB_STEP_SUMMARY"
	else
		printf '%s\n' "$*"
	fi
}

# owner/repo from the upstream URL, for messages.
upstream_label() {
	printf '%s/%s' "$(basename "$(dirname "$UPSTREAM_URL")")" "$(basename "$UPSTREAM_URL" .git)"
}

open_pr_number() {
	gh pr list --head "$SYNC_BRANCH" --base "$TARGET_BRANCH" --state open --json number |
		sed -n 's/.*"number": *\([0-9][0-9]*\).*/\1/p' | head -n 1
}

# Close the conflict PR if one is open and delete the sync branch if present. $1 = comment.
close_pr_and_branch() {
	local number
	number=$(open_pr_number)
	[ -z "$number" ] || gh pr close "$number" --comment "$1" >/dev/null
	if git ls-remote --exit-code --heads origin "$SYNC_BRANCH" >/dev/null 2>&1; then
		git push --quiet origin --delete "$SYNC_BRANCH"
	fi
}

# Sets base, count, commits_md, overlap_md for HEAD..upstream.
describe_range() {
	base=$(git merge-base HEAD "$upstream")
	count=$(git rev-list --count "$base..$upstream")
	commits_md=$(git log -n "$MAX_LISTED_COMMITS" --format='- %h %ad %an %s' --date=short "$base..$upstream")
	if [ "$count" -gt "$MAX_LISTED_COMMITS" ]; then
		commits_md="$commits_md"$'\n'"- … and $((count - MAX_LISTED_COMMITS)) more"
	fi
	overlap_md=$(comm -12 <(git diff --name-only "$base" HEAD | sort) <(git diff --name-only "$base" "$upstream" | sort) | sed 's/^/- /')
	[ -n "$overlap_md" ] || overlap_md="- none"
}

merge_message() {
	printf 'Sync DeepWorlds %s: %s commits (%s..%s)\n\nMerges %s %s into %s.\n\nCommits:\n%s\n\nOverlap with local changes since %s:\n%s\n' \
		"$UPSTREAM_BRANCH" "$count" "$(short "$base")" "$(short "$upstream")" \
		"$(upstream_label)" "$UPSTREAM_BRANCH" "$TARGET_BRANCH" "$commits_md" "$(short "$base")" "$overlap_md"
}

report_range() { # $1 = heading
	report "$1"
	report "$commits_md"
	report ""
	report "Overlap with local changes since $(short "$base"):"
	report "$overlap_md"
}

# ---- preconditions
current=$(git rev-parse --abbrev-ref HEAD)
[ "$current" = "$TARGET_BRANCH" ] || die "on '$current'; check out '$TARGET_BRANCH' first"
[ -z "$(git status --porcelain)" ] || die "working tree is not clean"
[ "$(git rev-parse --is-shallow-repository)" = "false" ] || die "shallow clone; full history is required"
is_dry || [ -n "${GH_TOKEN:-}" ] || die "GH_TOKEN is required unless DRY_RUN=true"

# ---- fetch the fork
if git remote get-url "$UPSTREAM_REMOTE" >/dev/null 2>&1; then
	git remote set-url "$UPSTREAM_REMOTE" "$UPSTREAM_URL"
else
	git remote add "$UPSTREAM_REMOTE" "$UPSTREAM_URL"
fi
git fetch --quiet "$UPSTREAM_REMOTE" "$UPSTREAM_BRANCH"
upstream=$(git rev-parse FETCH_HEAD)

if git merge-base --is-ancestor "$upstream" HEAD; then
	report "## Sync DeepWorlds: up to date"
	report "\`$TARGET_BRANCH\` already contains the fork tip \`$(short "$upstream")\`."
	is_dry || close_pr_and_branch "\`$TARGET_BRANCH\` already contains the fork tip \`$(short "$upstream")\`; superseded."
	exit 0
fi

describe_range

if merge_out=$(git merge --no-ff --no-edit -m "$(merge_message)" "$upstream" 2>&1); then
	if is_dry; then
		git reset --quiet --hard ORIG_HEAD
		report_range "## Sync DeepWorlds: dry run, would merge $count commits ($(short "$base")..$(short "$upstream"))"
		exit 0
	fi
	merge_sha=$(short HEAD)
	git push --quiet origin "HEAD:$TARGET_BRANCH"
	close_pr_and_branch "Merged cleanly in \`$merge_sha\`."
	report_range "## Sync DeepWorlds: merged $count commits in \`$merge_sha\`"
	exit 0
fi

die "conflict path not implemented yet: $merge_out"
