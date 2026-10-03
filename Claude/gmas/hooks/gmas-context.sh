#!/usr/bin/env bash
# SessionStart hook for the gmas plugin: tells Claude where GMC and GMAS live in this project,
# which GMAS generation it is, and which gmas skills to load for which job.
# Prints one JSON object (hookSpecificOutput.additionalContext) or nothing.
# Env: CLAUDE_GMAS_HOOK_DISABLE (any value) = do nothing; CLAUDE_GMAS_HOOK_DEBUG = trace on stderr.
set -u

debug() { if [ -n "${CLAUDE_GMAS_HOOK_DEBUG:-}" ]; then echo "gmas-context.sh: $*" >&2; fi; }

if [ -n "${CLAUDE_GMAS_HOOK_DISABLE:-}" ]; then debug "disabled"; exit 0; fi

has_uproject() {
	local f
	for f in "$1"/*.uproject; do [ -e "$f" ] && return 0; done
	return 1
}

emit() {
	local text="$1"
	text="${text//\\/\\\\}"
	text="${text//\"/\\\"}"
	printf '{"hookSpecificOutput":{"hookEventName":"SessionStart","additionalContext":"%s"}}\n' "$text"
}

# Walk up: the first directory with a .uproject is the project root. Remember a GMAS repository
# checkout seen on the way, used only when no project exists above it.
project_root=""
gmas_repo_root=""
dir="$PWD"
while [ -n "$dir" ]; do
	if has_uproject "$dir"; then project_root="$dir"; break; fi
	if [ -z "$gmas_repo_root" ] && [ -f "$dir/GMCAbilitySystem.uplugin" ]; then gmas_repo_root="$dir"; fi
	parent="$(dirname "$dir")"
	[ "$parent" = "$dir" ] && break
	dir="$parent"
done

if [ -z "$project_root" ]; then
	if [ -n "$gmas_repo_root" ]; then
		debug "GMAS repository checkout at $gmas_repo_root"
		emit "This is the GMAS plugin repository. Load gmas:gmas-maintain for the branch model, the DeepWorlds sync, the specs and the release recipe. Content here must stay generic: no downstream project names or paths, and no GMC source."
	else
		debug "no Unreal project above $PWD"
	fi
	exit 0
fi

# find_plugin_dir <root> <uplugin filename>: the plugin's directory relative to <root>, first match.
find_plugin_dir() {
	local f
	f="$(find "$1/Plugins" -mindepth 2 -maxdepth 4 -name "$2" -print 2>/dev/null | LC_ALL=C sort | head -n 1)"
	[ -n "$f" ] || return 1
	local d
	d="$(dirname "$f")"
	printf '%s\n' "${d#"$1"/}"
}

version_of() { # version_of <uplugin path>
	sed -n 's/.*"VersionName"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$1" | head -n 1
}

gmas_dir="$(find_plugin_dir "$project_root" GMCAbilitySystem.uplugin)" || gmas_dir=""
gmc_dir="$(find_plugin_dir "$project_root" GMC.uplugin)" || gmc_dir=""
debug "root=$project_root gmc=$gmc_dir gmas=$gmas_dir"

if [ -z "$gmas_dir" ]; then debug "no GMAS under Plugins/"; exit 0; fi

gmas_version="$(version_of "$project_root/$gmas_dir/GMCAbilitySystem.uplugin")"
[ -n "$gmas_version" ] || gmas_version="unknown version"
if [ -f "$project_root/$gmas_dir/Source/GMCAbilitySystem/Public/Utility/GMASBoundQueueV2.h" ]; then
	generation="1.4+ bound queue V2"
else
	generation="pre-1.4: bound queue V1, skill notes tagged 1.4+ do not apply"
fi

if [ -n "$gmc_dir" ]; then
	gmc_version="$(version_of "$project_root/$gmc_dir/GMC.uplugin")"
	[ -n "$gmc_version" ] || gmc_version="unknown version"
	gmc_text="GMC (General Movement Component) at \`$gmc_dir\` ($gmc_version)"
	gmc_source="GMC's source is licensed and exists only inside this project: read \`$gmc_dir/Source/GMCCore/Public/...\` for exact signatures and never copy GMC code into other repositories."
else
	gmc_text="GMC (General Movement Component), which GMAS requires"
	gmc_source="GMC was not found under \`Plugins/\`; it may be installed as an engine plugin (Engine/Plugins/Marketplace). GMC's source is licensed: read its headers where they are and never copy GMC code into other repositories."
fi

context="This project uses $gmc_text and GMAS (GMC Ability System) at \`$gmas_dir\` ($gmas_version, $generation). $gmc_source"
context="$context Load gmas:gmas-rules before editing predicted gameplay (gmas:gmc-prediction for GMC-only code); author with gmas:gmas-ability, gmas:gmas-effect, gmas:gmas-attribute, gmas:gmas-task; use gmas:gmas-debug for desync, replay or missing-effect issues, gmas:gmas-review for reviews, gmas:gmas-testing for automation tests, gmas:gmas-setup when wiring a new pawn, gmas:gmas-upgrade after updating GMAS."

emit "$context"
