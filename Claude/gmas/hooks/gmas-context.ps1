# SessionStart hook for the gmas plugin (PowerShell port of gmas-context.sh, same output): tells
# Claude where GMC and GMAS live in this project, which GMAS generation it is, and which gmas
# skills to load for which job. Prints one JSON object (hookSpecificOutput.additionalContext) or
# nothing, and always exits 0.
# Env: CLAUDE_GMAS_HOOK_DISABLE (any value) = do nothing; CLAUDE_GMAS_HOOK_DEBUG = trace on stderr.
# Runs under Windows PowerShell 5.1 and PowerShell 7 on any OS. Keep this file ASCII-only: 5.1
# reads a file without a byte order mark in the ANSI code page.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-HookTrace([string]$Message) {
    if ($env:CLAUDE_GMAS_HOOK_DEBUG) { [Console]::Error.WriteLine('gmas-context.ps1: ' + $Message) }
}

# JSON string body: the bash port escapes backslash and double quote; control characters and
# non-ASCII are escaped as \uXXXX as well so the output is ASCII whatever the console encoding.
function ConvertTo-JsonText([string]$Text) {
    $sb = New-Object System.Text.StringBuilder
    foreach ($ch in $Text.ToCharArray()) {
        $code = [int]$ch
        if ($ch -eq '\') { [void]$sb.Append('\\') }
        elseif ($ch -eq '"') { [void]$sb.Append('\"') }
        elseif ($code -lt 0x20 -or $code -gt 0x7E) { [void]$sb.Append(('\u{0:x4}' -f $code)) }
        else { [void]$sb.Append($ch) }
    }
    return $sb.ToString()
}

function Write-Context([string]$Text) {
    $json = '{"hookSpecificOutput":{"hookEventName":"SessionStart","additionalContext":"' + (ConvertTo-JsonText $Text) + '"}}'
    [Console]::Out.Write($json + "`n")
    [Console]::Out.Flush()
}

# True when <dir> holds a *.uproject entry (a dot-file does not count, as with a bash glob).
function Test-HasUproject([string]$Dir) {
    try {
        foreach ($entry in [System.IO.Directory]::EnumerateFileSystemEntries($Dir, '*.uproject')) {
            $name = [System.IO.Path]::GetFileName($entry)
            if (-not $name.StartsWith('.') -and $name -like '*.uproject') { return $true }
        }
    } catch { }
    return $false
}

# Subdirectories of <dir>, skipping links and junctions (find does not follow them either).
function Get-SubDirectories([string]$Dir) {
    $result = @()
    try {
        foreach ($sub in (New-Object System.IO.DirectoryInfo $Dir).EnumerateDirectories()) {
            if (($sub.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) { $result += $sub }
        }
    } catch { }
    return $result
}

# The plugin's directory relative to <root> with forward slashes ("Plugins/GMC"), first match in
# byte order, searching Plugins/*/<file> down to Plugins/*/*/*/<file>; $null when absent.
function Find-PluginDir([string]$Root, [string]$FileName) {
    $plugins = Join-Path $Root 'Plugins'
    if (-not [System.IO.Directory]::Exists($plugins)) { return $null }
    $hits = New-Object System.Collections.Generic.List[string]
    $level = @(@{ Full = $plugins; Rel = 'Plugins' })
    for ($depth = 1; $depth -le 3; $depth++) {
        $next = @()
        foreach ($d in $level) {
            foreach ($sub in (Get-SubDirectories $d.Full)) {
                $rel = $d.Rel + '/' + $sub.Name
                try {
                    foreach ($f in [System.IO.Directory]::EnumerateFiles($sub.FullName, $FileName)) {
                        if ([System.IO.Path]::GetFileName($f) -ceq $FileName) { $hits.Add($rel + '/' + $FileName) }
                    }
                } catch { }
                if ($depth -lt 3) { $next += @{ Full = $sub.FullName; Rel = $rel } }
            }
        }
        $level = $next
    }
    if ($hits.Count -eq 0) { return $null }
    $hits.Sort([System.StringComparer]::Ordinal)
    $first = $hits[0]
    return $first.Substring(0, $first.Length - $FileName.Length - 1)
}

# VersionName from a .uplugin (first line carrying it), '' when absent.
function Get-VersionName([string]$Path) {
    try {
        foreach ($line in [System.IO.File]::ReadAllLines($Path)) {
            $m = [regex]::Match($line, '^.*"VersionName"\s*:\s*"([^"]*)".*$')
            if ($m.Success) { return $m.Groups[1].Value }
        }
    } catch { }
    return ''
}

function Invoke-Hook {
    if ($env:CLAUDE_GMAS_HOOK_DISABLE) { Write-HookTrace 'disabled'; return }

    # Walk up: the first directory with a .uproject is the project root. Remember a GMAS repository
    # checkout seen on the way, used only when no project exists above it. The filesystem root /
    # is never examined (no project lives there); drive roots are.
    $start = (Get-Location).ProviderPath
    $projectRoot = $null
    $gmasRepoRoot = $null
    $dir = $start
    while ($dir) {
        if ($dir -eq '/') { break }
        if (Test-HasUproject $dir) { $projectRoot = $dir; break }
        if (-not $gmasRepoRoot -and [System.IO.File]::Exists((Join-Path $dir 'GMCAbilitySystem.uplugin'))) { $gmasRepoRoot = $dir }
        $parent = [System.IO.Path]::GetDirectoryName($dir)
        if (-not $parent -or $parent -eq $dir) { break }
        $dir = $parent
    }

    if (-not $projectRoot) {
        if ($gmasRepoRoot) {
            Write-HookTrace ('GMAS repository checkout at ' + $gmasRepoRoot)
            Write-Context 'This is the GMAS plugin repository. Load gmas:gmas-maintain for the branch model, the DeepWorlds sync, the specs and the release recipe. Content here must stay generic: no downstream project names or paths, and no GMC source.'
        } else {
            Write-HookTrace ('no Unreal project above ' + $start)
        }
        return
    }

    $gmasDir = Find-PluginDir $projectRoot 'GMCAbilitySystem.uplugin'
    $gmcDir = Find-PluginDir $projectRoot 'GMC.uplugin'
    Write-HookTrace ('root=' + $projectRoot + ' gmc=' + $gmcDir + ' gmas=' + $gmasDir)

    if (-not $gmasDir) { Write-HookTrace 'no GMAS under Plugins/'; return }

    $gmasVersion = Get-VersionName (Join-Path $projectRoot ($gmasDir + '/GMCAbilitySystem.uplugin'))
    if (-not $gmasVersion) { $gmasVersion = 'unknown' }
    if ([System.IO.File]::Exists((Join-Path $projectRoot ($gmasDir + '/Source/GMCAbilitySystem/Public/Utility/GMASBoundQueueV2.h')))) {
        $generation = '1.4+ bound queue V2'
    } else {
        $generation = 'pre-1.4: bound queue V1, skill notes tagged 1.4+ do not apply'
    }

    if ($gmcDir) {
        $gmcVersion = Get-VersionName (Join-Path $projectRoot ($gmcDir + '/GMC.uplugin'))
        if (-not $gmcVersion) { $gmcVersion = 'unknown version' }
        $gmcText = 'GMC (General Movement Component) at `' + $gmcDir + '` (' + $gmcVersion + ')'
        $gmcSource = 'GMC''s source is licensed and exists only inside this project: read `' + $gmcDir + '/Source/GMCCore/Public/...` for exact signatures and never copy GMC code into other repositories.'
    } else {
        $gmcText = 'GMC (General Movement Component), which GMAS requires,'
        $gmcSource = 'GMC was not found under `Plugins/`; it may be installed as an engine plugin (Engine/Plugins/Marketplace). GMC''s source is licensed: read its headers where they are and never copy GMC code into other repositories.'
    }

    $context = 'This project uses ' + $gmcText + ' and GMAS (GMC Ability System) at `' + $gmasDir + '` (VersionName ' + $gmasVersion + ', ' + $generation + '). ' + $gmcSource
    $context = $context + ' Load gmas:gmas-rules before editing predicted gameplay (gmas:gmc-prediction for GMC-only code); author with gmas:gmas-ability, gmas:gmas-effect, gmas:gmas-attribute, gmas:gmas-task; use gmas:gmas-debug for desync, replay or missing-effect issues, gmas:gmas-review for reviews, gmas:gmas-testing for automation tests, gmas:gmas-setup when wiring a new pawn, gmas:gmas-upgrade after updating GMAS.'

    Write-Context $context
}

# A SessionStart hook must never fail a session: every error is a debug trace and exit 0.
try { Invoke-Hook } catch { Write-HookTrace ('error: ' + $_.Exception.Message) }
exit 0
