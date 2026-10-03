# Structural checks for the gmas Claude Code plugin; PowerShell port of check.sh with the same
# numbered checks, output and exit code. Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File Claude/gmas/scripts/check.ps1 [repo-root]
# Prints one line per check; final line "N passed, M failed" (", K skipped" appended when checks
# were skipped); exit 1 when anything failed. $env:GMAS_CHECK_FAST = '1' skips the slow checks
# (manifest validation, hook harnesses) and prints a "skip" line for each. Needs the claude CLI on
# PATH; no python. Check 8 runs the PowerShell hook harness and, when Git Bash (or bash on
# macOS/Linux) is available, the bash one too. Keep this file ASCII-only (5.1 reads ANSI).
param([string]$RepoRoot)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
if ($RepoRoot) { $Root = [System.IO.Path]::GetFullPath($RepoRoot) } else { $Root = [System.IO.Path]::GetFullPath((Join-Path $Here '../../..')) }
$Plugin = Join-Path $Root 'Claude/gmas'
$Market = Join-Path $Root '.claude-plugin/marketplace.json'
$Fast = ($env:GMAS_CHECK_FAST -eq '1')
$OnWindows = [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT

$ExpectedSkills = @('gmc-prediction', 'gmas-rules', 'gmas-setup', 'gmas-ability', 'gmas-effect', 'gmas-attribute', 'gmas-task', 'gmas-debug', 'gmas-review', 'gmas-testing', 'gmas-upgrade', 'gmas-maintain')
$MaxLines = 300
# Downstream names that must never appear (kept here, not in the skills).
$LeakWords = 'Iliad|Seek|Cynosure|OmegaShooters|MCPTesting'
# Drive paths (a letter, a colon, then a slash or backslash; not the scheme separator inside
# https://) and home directories.
$LeakPaths = '(^|[^A-Za-z])[A-Za-z]:[\\/]|/Users/|/home/[a-z]'
# The checker scripts carry the patterns and the harnesses' injected violations.
$CheckerFiles = @('check.sh', 'check.test.sh', 'check.ps1', 'check.test.ps1')

$script:Pass = 0; $script:Fail = 0; $script:Skip = 0
function Write-Line([string]$Text) { [Console]::Out.WriteLine($Text) }
function Ok([string]$Name) { $script:Pass++; Write-Line ('ok   - ' + $Name) }
function Skip([string]$Name) { $script:Skip++; Write-Line ('skip - ' + $Name) }
function Fail([string]$Name, [string]$Detail = '') {
    $script:Fail++
    Write-Line ('FAIL - ' + $Name)
    if ($Detail) { foreach ($l in ($Detail.TrimEnd("`r", "`n") -split "`r?`n")) { Write-Line ('       ' + $l) } }
}

# One argument quoted for a Windows command line (C runtime rules).
function ConvertTo-NativeArg([string]$Arg) {
    if ($Arg -ne '' -and $Arg -notmatch '[\s"]') { return $Arg }
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append('"')
    $slashes = 0
    foreach ($ch in $Arg.ToCharArray()) {
        if ($ch -eq '\') { $slashes++; continue }
        if ($ch -eq '"') { [void]$sb.Append('\' * (2 * $slashes + 1)); [void]$sb.Append('"') }
        else { [void]$sb.Append('\' * $slashes); [void]$sb.Append($ch) }
        $slashes = 0
    }
    [void]$sb.Append('\' * (2 * $slashes))
    [void]$sb.Append('"')
    return $sb.ToString()
}

# Runs <file> <args> with stdin closed and a time limit; returns Out (stdout then stderr), Code
# (-1 on timeout, after killing the process).
function Invoke-Captured([string]$File, [string[]]$Arguments, [int]$TimeoutSec) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $File
    $psi.Arguments = (@($Arguments | ForEach-Object { ConvertTo-NativeArg $_ })) -join ' '
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = New-Object System.Text.UTF8Encoding $false
    $psi.StandardErrorEncoding = New-Object System.Text.UTF8Encoding $false
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.StandardInput.Close()
    $outTask = $p.StandardOutput.ReadToEndAsync()
    $errTask = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($TimeoutSec * 1000)) {
        try { $p.Kill() } catch { }
        return [pscustomobject]@{ Out = ('timed out after ' + $TimeoutSec + ' s'); Code = -1 }
    }
    $p.WaitForExit()
    return [pscustomobject]@{ Out = ($outTask.Result + $errTask.Result); Code = $p.ExitCode }
}

function Get-LastLine([string]$Text) {
    $lines = @($Text.TrimEnd("`r", "`n") -split "`r?`n")
    return $lines[$lines.Count - 1]
}

# All files under <dir>, full paths in byte order.
function Get-FilesSorted([string]$Dir, [string]$Filter = '*') {
    $list = New-Object System.Collections.Generic.List[string]
    if ([System.IO.Directory]::Exists($Dir)) {
        foreach ($f in [System.IO.Directory]::EnumerateFiles($Dir, $Filter, [System.IO.SearchOption]::AllDirectories)) { $list.Add($f) }
    }
    $list.Sort([System.StringComparer]::Ordinal)
    return $list.ToArray()
}

# File lines split on LF only (a CR stays on its line, as with grep and awk).
function Get-Lines([string]$Path) {
    $text = [System.IO.File]::ReadAllText($Path)
    if ($text.EndsWith("`n")) { $text = $text.Substring(0, $text.Length - 1) }
    if ($text -eq '') { return , @() }
    return , ($text -split "`n")
}

# grep -n over the plugin files and the marketplace, checker scripts excluded.
function Find-Pattern([string]$Pattern) {
    $files = @(Get-FilesSorted $Plugin)
    if ([System.IO.File]::Exists($Market)) { $files += $Market }
    $hits = New-Object System.Collections.Generic.List[string]
    foreach ($f in $files) {
        if ($CheckerFiles -contains [System.IO.Path]::GetFileName($f)) { continue }
        $lines = Get-Lines $f
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ([regex]::IsMatch($lines[$i], $Pattern)) { $hits.Add($f + ':' + ($i + 1) + ':' + $lines[$i]) }
        }
    }
    return ($hits -join "`n")
}

# Git Bash on Windows (never the WSL launcher in System32 or WindowsApps), bash elsewhere; $null if none.
function Find-Bash {
    $candidates = @(Get-Command bash -CommandType Application -All -ErrorAction SilentlyContinue | ForEach-Object { $_.Source })
    if (-not $OnWindows) { if ($candidates.Count -gt 0) { return $candidates[0] }; return $null }
    foreach ($c in $candidates) {
        if ($c -like ($env:SystemRoot + '\*') -or $c -like '*\WindowsApps\*') { continue }
        return $c
    }
    $git = Get-Command git -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($git) {
        # git.exe lives in <Git>\cmd or <Git>\bin or <Git>\mingw64\bin; bash.exe in <Git>\bin.
        $dir = Split-Path -Parent $git.Source
        foreach ($guess in @((Join-Path $dir 'bash.exe'), (Join-Path $dir '../bin/bash.exe'), (Join-Path $dir '../../bin/bash.exe'))) {
            if ([System.IO.File]::Exists($guess)) { return [System.IO.Path]::GetFullPath($guess) }
        }
    }
    return $null
}

# 1. Manifests validate (strict). Skipped in fast mode.
if ($Fast) {
    Skip 'manifests validate (GMAS_CHECK_FAST)'
} else {
    $claude = Get-Command claude -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($claude) {
        foreach ($m in @(@{ Name = 'plugin manifest validates'; Path = $Plugin }, @{ Name = 'marketplace manifest validates'; Path = $Market })) {
            $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
            $out = (& $claude.Source plugin validate --strict $m.Path 2>&1 | ForEach-Object { [string]$_ }) -join "`n"
            $rc = $LASTEXITCODE
            $ErrorActionPreference = $prev
            if ($rc -eq 0) { Ok $m.Name } else { Fail $m.Name $out }
        }
    } else {
        Fail "claude CLI available for 'plugin validate'" 'install Claude Code'
    }
}

# 2. Expected skills present.
$missing = ''
foreach ($s in $ExpectedSkills) { if (-not [System.IO.File]::Exists((Join-Path $Plugin "skills/$s/SKILL.md"))) { $missing += ' ' + $s } }
if (-not $missing) { Ok 'all expected skills present' } else { Fail ('all expected skills present (missing:' + $missing + ')') }

# 3. Frontmatter: name == dir, description starts with "Use when", block <= 1024 chars.
$skillDirs = New-Object System.Collections.Generic.List[string]
if ([System.IO.Directory]::Exists((Join-Path $Plugin 'skills'))) {
    foreach ($d in [System.IO.Directory]::EnumerateDirectories((Join-Path $Plugin 'skills'))) { $skillDirs.Add($d) }
}
$skillDirs.Sort([System.StringComparer]::Ordinal)
foreach ($sd in $skillDirs) {
    $f = Join-Path $sd 'SKILL.md'
    if (-not [System.IO.File]::Exists($f)) { continue }
    $dir = [System.IO.Path]::GetFileName($sd)
    $lines = Get-Lines $f
    $fmLines = @()
    if ($lines.Count -gt 0 -and $lines[0] -eq '---') {
        for ($i = 1; $i -lt $lines.Count; $i++) { if ($lines[$i] -eq '---') { break }; $fmLines += $lines[$i] }
    }
    $fm = $fmLines -join "`n"
    $name = ''; $desc = ''
    foreach ($l in $fmLines) { $m = [regex]::Match($l, '^name:\s*(.*)$'); if ($m.Success) { $name = $m.Groups[1].Value; break } }
    foreach ($l in $fmLines) { $m = [regex]::Match($l, '^description:\s*(.*)$'); if ($m.Success) { $desc = $m.Groups[1].Value; break } }
    if ($name -ceq $dir) { Ok "${dir}: frontmatter name equals directory" } else { Fail "${dir}: frontmatter name equals directory" "name='$name'" }
    if ($desc.StartsWith('Use when', [System.StringComparison]::Ordinal)) { Ok "${dir}: description starts with 'Use when'" } else { Fail "${dir}: description starts with 'Use when'" "description='$desc'" }
    if ($fm.Length -le 1024) { Ok "${dir}: frontmatter <= 1024 chars" } else { Fail "${dir}: frontmatter <= 1024 chars" ("$($fm.Length) chars") }
    $n = ([System.IO.File]::ReadAllText($f).Split("`n").Count - 1)
    if ($n -le $MaxLines) { Ok "${dir}: SKILL.md <= $MaxLines lines" } else { Fail "${dir}: SKILL.md <= $MaxLines lines ($n)" }
}

# 4. Relative links resolve (markdown links not starting with a scheme or '#').
$broken = ''
foreach ($f in @(Get-FilesSorted $Plugin '*.md')) {
    $d = Split-Path -Parent $f
    foreach ($m in [regex]::Matches([System.IO.File]::ReadAllText($f), '\]\(([^)\n]+)\)')) {
        $target = $m.Groups[1].Value
        $t = $target.Split('#')[0]
        if (-not $t) { continue }
        if ($t.StartsWith('http://') -or $t.StartsWith('https://') -or $t.StartsWith('mailto:')) { continue }
        $exists = $false
        try { $exists = Test-Path -LiteralPath (Join-Path $d $t) } catch { }
        if (-not $exists) { $broken += "`n" + $f + ' -> ' + $target }
    }
}
if (-not $broken) { Ok 'relative links resolve' } else { Fail 'relative links resolve (broken link)' $broken.TrimStart("`n") }

# 5. hooks.json parses (when present).
$hooksJson = Join-Path $Plugin 'hooks/hooks.json'
if ([System.IO.File]::Exists($hooksJson)) {
    try { [void](ConvertFrom-Json ([System.IO.File]::ReadAllText($hooksJson))); Ok 'hooks.json parses' } catch { Fail 'hooks.json parses' }
}

# 6. Leak grep over the plugin and the marketplace (checker scripts excluded).
$hits = ((Find-Pattern $LeakWords), (Find-Pattern $LeakPaths) | Where-Object { $_ }) -join "`n"
if (-not $hits) { Ok 'leak grep clean' } else { Fail 'leak grep clean (leak)' $hits }

# 7. GMC excerpt guard: no GRIMTEC/author copyright lines; no fenced block defining a GMC symbol.
$hits = Find-Pattern 'GRIMTEC|Dominik Lips|Copyright.*GMC'
if (-not $hits) { Ok 'no GMC copyright lines' } else { Fail 'no GMC copyright lines (GMC excerpt)' $hits }
# A fence line (``` or ~~~, possibly indented inside a list item) toggles the in-fence state; the
# state resets at the start of every file so an unterminated fence cannot hide the files after it.
$hitList = New-Object System.Collections.Generic.List[string]
foreach ($f in @(Get-FilesSorted $Plugin '*.md')) {
    $inFence = $false
    $lines = Get-Lines $f
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^\s*(```|~~~)') { $inFence = -not $inFence; continue }
        if ($inFence -and $lines[$i] -match '(GMCCORE_API|UGMC_ReplicationCmp::|UGMC_MovementUtilityCmp::|UGMC_OrganicMovementCmp::)') {
            $hitList.Add($f + ':' + ($i + 1) + ': ' + $lines[$i])
        }
    }
}
if ($hitList.Count -eq 0) { Ok 'no GMC definitions in code blocks' } else { Fail 'no GMC definitions in code blocks (GMC excerpt)' ($hitList -join "`n") }

# 8. Hook harnesses (when present). Skipped in fast mode. Each runs with stdin closed under a
#    120 s limit so a hung harness cannot hang the check.
$psHarness = Join-Path $Plugin 'hooks/tests/gmas-context.test.ps1'
$shHarness = Join-Path $Plugin 'hooks/tests/gmas-context.test.sh'
if ([System.IO.File]::Exists($psHarness) -or [System.IO.File]::Exists($shHarness)) {
    if ($Fast) {
        Skip 'hook harness (GMAS_CHECK_FAST)'
    } else {
        if ([System.IO.File]::Exists($psHarness)) {
            $self = [System.Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
            $r = Invoke-Captured $self @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $psHarness) 120
            if ($r.Code -eq 0) { Ok ('hook harness: ' + (Get-LastLine $r.Out)) } else { Fail ('hook harness (exit ' + $r.Code + ')') $r.Out }
        }
        if ([System.IO.File]::Exists($shHarness)) {
            $bash = Find-Bash
            if ($bash) {
                $r = Invoke-Captured $bash @($shHarness.Replace('\', '/')) 120
                if ($r.Code -eq 0) { Ok ('hook harness (bash): ' + (Get-LastLine $r.Out)) } else { Fail ('hook harness (bash) (exit ' + $r.Code + ')') $r.Out }
            } else {
                Skip 'hook harness (bash): no bash found'
            }
        }
    }
}

$summary = "$($script:Pass) passed, $($script:Fail) failed"
if ($script:Skip -gt 0) { $summary += ", $($script:Skip) skipped" }
Write-Line $summary
if ($script:Fail -eq 0) { exit 0 } else { exit 1 }
