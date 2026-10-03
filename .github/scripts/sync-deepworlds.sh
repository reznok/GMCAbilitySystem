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
CANONICAL_URL="${CANONICAL_URL:-https://github.com/reznok/GMCAbilitySystem.git}"
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
	gh pr list --head "$SYNC_BRANCH" --base "$TARGET_BRANCH" --state open --json number --jq '.[0].number // empty'
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
	base=$(git merge-base HEAD "$upstream") || die "no common history with the fork"
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

pr_body() {
	cat <<EOF
Automated merge of $(upstream_label) \`$UPSTREAM_BRANCH\` ($(short "$base")..$(short "$upstream")) into \`$TARGET_BRANCH\` failed.

## Conflicting files
$conflicts_md

## Overlap with local changes since $(short "$base")
$overlap_md

## Commits
$commits_md

## Resolve locally
    git fetch origin $TARGET_BRANCH $SYNC_BRANCH
    git checkout $TARGET_BRANCH
    git pull --ff-only
    git merge --no-ff origin/$SYNC_BRANCH
    # resolve conflicts, build locally, then
    git push origin $TARGET_BRANCH

Do not merge this pull request on GitHub: it carries the unresolved fork tip. The next scheduled run closes it and deletes \`$SYNC_BRANCH\` once \`$TARGET_BRANCH\` contains $(short "$upstream").
EOF
}

# ---- preconditions
current=$(git rev-parse --abbrev-ref HEAD)
[ "$current" = "$TARGET_BRANCH" ] || die "on '$current'; check out '$TARGET_BRANCH' first"
[ -z "$(git status --porcelain)" ] || die "working tree is not clean"
[ "$(git rev-parse --is-shallow-repository)" = "false" ] || die "shallow clone; full history is required"
is_dry || [ -n "${GH_TOKEN:-}" ] || die "GH_TOKEN is required unless DRY_RUN=true"
origin_url=$(git remote get-url origin 2>/dev/null || true)
case "$origin_url" in
	"$CANONICAL_URL"|"${CANONICAL_URL%.git}"|"${CANONICAL_URL%.git}.git") ;;
	*) is_dry || die "origin is '$origin_url', not the canonical repository ($CANONICAL_URL); set CANONICAL_URL to override" ;;
esac

# ---- fetch the fork
if git remote get-url "$UPSTREAM_REMOTE" >/dev/null 2>&1; then
	git remote set-url "$UPSTREAM_REMOTE" "$UPSTREAM_URL"
else
	git remote add "$UPSTREAM_REMOTE" "$UPSTREAM_URL"
fi
git fetch --quiet "$UPSTREAM_REMOTE" "$UPSTREAM_BRANCH"
upstream=$(git rev-parse FETCH_HEAD)

# ---- merge, pushing at most twice: a push rejected because TARGET_BRANCH moved
# refetches it and redoes the merge once from the new tip.
attempt=1
while :; do
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
		if git push --quiet origin "HEAD:$TARGET_BRANCH"; then
			report_range "## Sync DeepWorlds: merged $count commits in \`$merge_sha\`"
			[ "$attempt" -eq 1 ] || report "(push succeeded on attempt $attempt)"
			close_pr_and_branch "Merged cleanly in \`$merge_sha\`."
			exit 0
		fi
		[ "$attempt" -lt 2 ] || die "push to $TARGET_BRANCH rejected twice"
		log "push rejected; refetching $TARGET_BRANCH and retrying"
		attempt=$((attempt + 1))
		git fetch --quiet origin "$TARGET_BRANCH"
		git reset --quiet --hard FETCH_HEAD
		continue
	fi

	# ---- conflict
	conflicts=$(git diff --name-only --diff-filter=U)
	git merge --abort 2>/dev/null || true
	[ -n "$conflicts" ] || die "merge failed without conflicts: $merge_out"
	conflict_count=$(printf '%s\n' "$conflicts" | grep -c .)
	conflicts_md=$(printf '%s\n' "$conflicts" | sed 's/^/- /')

	if is_dry; then
		report "## Sync DeepWorlds: dry run, $count commits conflict in $conflict_count files"
		report "$conflicts_md"
		report ""
		report_range "Commits:"
		exit 0
	fi

	git push --quiet --force origin "$upstream:refs/heads/$SYNC_BRANCH"
	gh label create "$LABEL" --color C5DEF5 --description "Automated fork sync" --force >/dev/null
	title="Sync DeepWorlds $UPSTREAM_BRANCH: $count commits, conflicts in $conflict_count files"
	body=$(pr_body)
	number=$(open_pr_number)
	if [ -n "$number" ]; then
		gh pr edit "$number" --title "$title" --body "$body" >/dev/null
		report "## Sync DeepWorlds: conflicts in $conflict_count files; pull request updated"
	else
		gh pr create --head "$SYNC_BRANCH" --base "$TARGET_BRANCH" --title "$title" --body "$body" --label "$LABEL" >/dev/null
		report "## Sync DeepWorlds: conflicts in $conflict_count files; pull request created"
	fi
	report "$conflicts_md"
	exit 0
done
