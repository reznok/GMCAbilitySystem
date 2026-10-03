# Harness for scripts/check.ps1 (PowerShell port of check.test.sh, same scenarios S1 to S11):
# copies the plugin into a temp tree, injects one violation per scenario and asserts that
# check.ps1 fails with the expected message (and passes when clean). S1 runs the full check once;
# every doctored scenario runs with GMAS_CHECK_FAST=1, which skips manifest validation and the
# hook harnesses (slow, and not what those scenarios test).
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File Claude/gmas/scripts/tests/check.test.ps1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Repo = [System.IO.Path]::GetFullPath((Join-Path $Here '../../../..'))
$Check = Join-Path $Repo 'Claude/gmas/scripts/check.ps1'
$PowerShellExe = [System.Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
$Work = Join-Path ([System.IO.Path]::GetTempPath()) ('gmas-check-test-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $Work)
$Skills = @('gmc-prediction', 'gmas-rules', 'gmas-setup', 'gmas-ability', 'gmas-effect', 'gmas-attribute', 'gmas-task', 'gmas-debug', 'gmas-review', 'gmas-testing', 'gmas-upgrade', 'gmas-maintain')

$script:Pass = 0; $script:Fail = 0
function Ok([string]$Name) { $script:Pass++; [Console]::Out.WriteLine('ok   - ' + $Name) }
function Fail([string]$Name) { $script:Fail++; [Console]::Out.WriteLine('FAIL - ' + $Name) }
function Write-Tail([string]$Text, [int]$Count) {
    $lines = @($Text.TrimEnd("`r", "`n") -split "`r?`n")
    $from = [Math]::Max(0, $lines.Count - $Count)
    for ($i = $from; $i -lt $lines.Count; $i++) { [Console]::Out.WriteLine($lines[$i]) }
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

# Runs check.ps1 on <root>; returns Out (stdout then stderr) and Code. <fast> sets GMAS_CHECK_FAST.
function Invoke-Check([string]$Root, [bool]$FastMode) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $PowerShellExe
    $psi.Arguments = (@('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $Check, $Root) | ForEach-Object { ConvertTo-NativeArg $_ }) -join ' '
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = New-Object System.Text.UTF8Encoding $false
    $psi.StandardErrorEncoding = New-Object System.Text.UTF8Encoding $false
    # An inherited GMAS_CHECK_FAST would silently make S1 the fast path, so set it either way.
    if ($FastMode) { $psi.EnvironmentVariables['GMAS_CHECK_FAST'] = '1' } else { [void]$psi.EnvironmentVariables.Remove('GMAS_CHECK_FAST') }
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.StandardInput.Close()
    $errTask = $p.StandardError.ReadToEndAsync()
    $out = $p.StandardOutput.ReadToEnd()
    $p.WaitForExit()
    return [pscustomobject]@{ Out = ($out + $errTask.Result); Code = $p.ExitCode }
}

# A copy of the plugin + marketplace under $Work/<name>, with stubs for missing skills.
function New-FreshCopy([string]$Name) {
    $dst = Join-Path $Work $Name
    [void](New-Item -ItemType Directory -Force -Path (Join-Path $dst 'Claude'))
    [void](New-Item -ItemType Directory -Force -Path (Join-Path $dst '.claude-plugin'))
    Copy-Item -Recurse -LiteralPath (Join-Path $Repo 'Claude/gmas') -Destination (Join-Path $dst 'Claude/gmas')
    Copy-Item -LiteralPath (Join-Path $Repo '.claude-plugin/marketplace.json') -Destination (Join-Path $dst '.claude-plugin/marketplace.json')
    foreach ($s in $Skills) {
        $f = Join-Path $dst "Claude/gmas/skills/$s/SKILL.md"
        if (-not [System.IO.File]::Exists($f)) {
            [void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $f))
            [System.IO.File]::WriteAllText($f, "---`nname: $s`ndescription: Use when testing the check script.`n---`n`n# $s`n`nBody.`n")
        }
    }
    return $dst
}

function Add-Text([string]$Path, [string]$Text) { [System.IO.File]::AppendAllText($Path, $Text) }
function Edit-Text([string]$Path, [string]$Pattern, [string]$Replacement) {
    $t = [System.IO.File]::ReadAllText($Path)
    [System.IO.File]::WriteAllText($Path, [regex]::Replace($t, $Pattern, $Replacement, [System.Text.RegularExpressions.RegexOptions]::Multiline))
}
function Skill([string]$Root, [string]$Name) { return (Join-Path $Root "Claude/gmas/skills/$Name/SKILL.md") }

function Assert-Pass([string]$Name, [string]$Root, [bool]$FastMode = $true) {
    $r = Invoke-Check $Root $FastMode
    if ($r.Code -eq 0) { Ok $Name } else { Fail $Name; Write-Tail $r.Out 5 }
}
# check.ps1 must fail, and a FAIL line must mention <substring>.
function Assert-Fail([string]$Name, [string]$Root, [string]$Substring) {
    $r = Invoke-Check $Root $true
    $failLines = @(($r.Out -split "`r?`n") | Where-Object { $_.StartsWith('FAIL') })
    if ($r.Code -eq 0) { Fail ($Name + ' (check passed, expected failure)') }
    elseif (@($failLines | Where-Object { $_.Contains($Substring) }).Count -gt 0) { Ok $Name }
    else { Fail ($Name + ' (failed for the wrong reason)'); Write-Tail ($failLines -join "`n") 8 }
}

try {
    # S1: the real tree passes, on the full (slow) path.
    $R = New-FreshCopy 'clean'
    Assert-Pass 'S1 clean tree passes' $R $false

    # S2: a downstream project name fails the leak grep.
    $R = New-FreshCopy 'leak'
    Add-Text (Skill $R 'gmas-rules') "Seen in Cynosure once.`n"   # injected violation
    Assert-Fail 'S2 leak grep catches a project name' $R 'leak'

    # S3: a drive path fails the leak grep; a URL does not.
    $R = New-FreshCopy 'drive'
    Add-Text (Skill $R 'gmas-rules') "See https://example.com/docs and D:/Work/Game.`n"   # injected violation
    Assert-Fail 'S3 leak grep catches a drive path' $R 'leak'
    $R = New-FreshCopy 'url'
    Add-Text (Skill $R 'gmas-rules') "See https://example.com/docs only.`n"
    Assert-Pass 'S3b a URL alone passes' $R

    # S4: description must start with "Use when".
    $R = New-FreshCopy 'desc'
    Edit-Text (Skill $R 'gmas-debug') '^description: Use when' 'description: Helps with'
    Assert-Fail 'S4 description trigger rule' $R 'Use when'

    # S5: name must equal the directory.
    $R = New-FreshCopy 'name'
    Edit-Text (Skill $R 'gmas-task') '^name: gmas-task$' 'name: gmas-tasks'
    Assert-Fail 'S5 name equals directory' $R 'name equals'

    # S6: SKILL.md over 300 lines fails.
    $R = New-FreshCopy 'long'
    Add-Text (Skill $R 'gmas-effect') ((@(1..310 | ForEach-Object { "line $_" }) -join "`n") + "`n")
    Assert-Fail 'S6 line budget' $R '300'

    # S7: a GMC copyright line fails the excerpt guard.
    $R = New-FreshCopy 'grim'
    Add-Text (Skill $R 'gmc-prediction') "// Copyright GRIMTEC`n"
    Assert-Fail 'S7 GMC excerpt guard (copyright)' $R 'copyright'

    # S8: a fenced block defining a GMC symbol fails the excerpt guard.
    $R = New-FreshCopy 'excerpt'
    Add-Text (Skill $R 'gmc-prediction') "`n``````cpp`nvoid UGMC_ReplicationCmp::Foo()`n{`n}`n```````n"
    Assert-Fail 'S8 GMC excerpt guard (definition)' $R 'definitions in code blocks'

    # S8b: an unterminated fence in an earlier file must not hide a definition in a later one
    # (check.ps1 scans the files in sorted order; gmas-ability sorts before gmc-prediction).
    $R = New-FreshCopy 'carry'
    Add-Text (Skill $R 'gmas-ability') "`n```````nunterminated fence`n"
    Add-Text (Skill $R 'gmc-prediction') "`n``````cpp`nvoid UGMC_ReplicationCmp::Foo()`n{`n}`n```````n"
    Assert-Fail 'S8b GMC excerpt guard (fence state resets per file)' $R 'definitions in code blocks'

    # S8c: a fence indented inside a list item still opens a code block.
    $R = New-FreshCopy 'indent'
    Add-Text (Skill $R 'gmc-prediction') "`n- Example:`n`n  ``````cpp`n  void UGMC_ReplicationCmp::Foo()`n  {`n  }`n  ```````n"
    Assert-Fail 'S8c GMC excerpt guard (indented fence)' $R 'definitions in code blocks'

    # S9: a broken relative link fails.
    $R = New-FreshCopy 'link'
    Add-Text (Skill $R 'gmas-setup') "See [missing](references/nope.md).`n"
    Assert-Fail 'S9 relative links resolve' $R 'link'

    # S10: a missing expected skill fails.
    $R = New-FreshCopy 'missing'
    Remove-Item -Recurse -Force -LiteralPath (Join-Path $R 'Claude/gmas/skills/gmas-upgrade')
    Assert-Fail 'S10 expected skills present' $R 'gmas-upgrade'

    # S11: unparsable hooks.json fails.
    $R = New-FreshCopy 'hooks'
    [System.IO.File]::WriteAllText((Join-Path $R 'Claude/gmas/hooks/hooks.json'), "{ not json`n")
    Assert-Fail 'S11 hooks.json parses' $R 'hooks.json'
} finally {
    Remove-Item -Recurse -Force -LiteralPath $Work -ErrorAction SilentlyContinue
}

[Console]::Out.WriteLine(('{0} passed, {1} failed' -f $script:Pass, $script:Fail))
if ($script:Fail -eq 0) { exit 0 } else { exit 1 }
