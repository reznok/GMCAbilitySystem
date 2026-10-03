# Harness for hooks/gmas-context.ps1 (PowerShell port of gmas-context.test.sh, same fixtures F1 to
# F8) plus F9, the hooks.json command line run the way Claude Code runs it on Windows without Git
# Bash. Every assertion records ok or FAIL and continues; the final line is "N passed, M failed".
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File Claude/gmas/hooks/tests/gmas-context.test.ps1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Hook = [System.IO.Path]::GetFullPath((Join-Path $Here '../gmas-context.ps1'))
$PluginRoot = [System.IO.Path]::GetFullPath((Join-Path $Here '../..'))
$PowerShellExe = [System.Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
$Work = Join-Path ([System.IO.Path]::GetTempPath()) ('gmas-hook-test-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $Work)

$script:Pass = 0
$script:Fail = 0
function Ok([string]$Name) { $script:Pass++; [Console]::Out.WriteLine('ok   - ' + $Name) }
function Fail([string]$Name, [string]$Detail = '') {
    $script:Fail++
    [Console]::Out.WriteLine('FAIL - ' + $Name)
    if ($Detail) { foreach ($l in ($Detail -split "`r?`n")) { [Console]::Out.WriteLine('       ' + $l) } }
}

# One argument quoted for a Windows command line (the C runtime rules Node and .NET follow).
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

# Runs <file> <args> in <cwd> with stdin closed; <env> entries override (a $null value removes the
# variable). Returns Out, Err and Code.
function Invoke-Captured([string]$File, [string[]]$Arguments, [string]$Cwd, [hashtable]$Env) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $File
    $psi.Arguments = (@($Arguments | ForEach-Object { ConvertTo-NativeArg $_ })) -join ' '
    $psi.WorkingDirectory = $Cwd
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = New-Object System.Text.UTF8Encoding $false
    $psi.StandardErrorEncoding = New-Object System.Text.UTF8Encoding $false
    if ($Env) {
        foreach ($k in $Env.Keys) {
            if ($null -eq $Env[$k]) { [void]$psi.EnvironmentVariables.Remove($k) } else { $psi.EnvironmentVariables[$k] = [string]$Env[$k] }
        }
    }
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.StandardInput.Close()
    $errTask = $p.StandardError.ReadToEndAsync()
    $out = $p.StandardOutput.ReadToEnd()
    $p.WaitForExit()
    return [pscustomobject]@{ Out = $out; Err = $errTask.Result; Code = $p.ExitCode }
}

# Runs the hook from <dir>; sets $script:Out (stdout, trailing newline trimmed) and $script:Rc.
$script:Out = ''
$script:Rc = 0
function Invoke-Hook([string]$Dir, [hashtable]$Extra = @{}) {
    $vars = @{ CLAUDE_GMAS_HOOK_DISABLE = $null; CLAUDE_GMAS_HOOK_DEBUG = $null }
    foreach ($k in $Extra.Keys) { $vars[$k] = $Extra[$k] }
    $r = Invoke-Captured $PowerShellExe @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $Hook) $Dir $vars
    $script:Out = $r.Out.TrimEnd("`r", "`n")
    $script:Rc = $r.Code
}

function Assert-Exit0([string]$Name) {
    if ($script:Rc -eq 0) { Ok ($Name + ': exit 0') }
    else { $d = 'exit ' + $script:Rc; if ($script:Out) { $d += '; output: ' + $script:Out }; Fail ($Name + ': exit 0') $d }
}

# Sets $script:Ctx to additionalContext; unparsable output records a failure and leaves it empty.
$script:Ctx = ''
function Get-Context([string]$Name, [string]$Json) {
    $script:Ctx = ''
    try {
        $script:Ctx = [string](ConvertFrom-Json $Json).hookSpecificOutput.additionalContext
        if (-not $script:Ctx) { throw 'no additionalContext' }
    } catch {
        Fail ($Name + ': JSON with additionalContext') ($_.Exception.Message + "`noutput: " + $Json)
        $script:Ctx = ''
    }
}

function Assert-Contains([string]$Name, [string]$Hay, [string[]]$Needles) {
    foreach ($n in $Needles) {
        if (-not $Hay.Contains($n)) { Fail ($Name + ": contains '" + $n + "'") $Hay; return }
    }
    Ok $Name
}

function New-Dir([string]$Path) { [void](New-Item -ItemType Directory -Force -Path $Path) }
function New-EmptyFile([string]$Path) { New-Dir (Split-Path -Parent $Path); [System.IO.File]::WriteAllText($Path, '') }
function New-Uplugin([string]$Path, [string]$Version) {
    New-Dir (Split-Path -Parent $Path)
    [System.IO.File]::WriteAllText($Path, "{`n`t`"FileVersion`": 3,`n`t`"VersionName`": `"$Version`",`n`t`"Modules`": []`n}`n")
}

try {
    # F1: no project anywhere -> silence.
    New-Dir "$Work/none/deep"
    Invoke-Hook "$Work/none/deep"
    if (-not $script:Out) { Ok 'F1 no project: silent' } else { Fail 'F1 no project: silent' $script:Out }
    Assert-Exit0 'F1 no project'

    # F2: project without plugins -> silence.
    New-Dir "$Work/p2/Source"; New-EmptyFile "$Work/p2/Game.uproject"
    Invoke-Hook "$Work/p2/Source"
    if (-not $script:Out) { Ok 'F2 project without GMAS: silent' } else { Fail 'F2 project without GMAS: silent' $script:Out }
    Assert-Exit0 'F2 project without GMAS'

    # F3: pre-1.4 GMAS copy under another folder name, no GMC.
    New-EmptyFile "$Work/p3/Game.uproject"
    New-Uplugin "$Work/p3/Plugins/DeepWorlds_GMCAbilitySystem/GMCAbilitySystem.uplugin" '1.3'
    Invoke-Hook "$Work/p3"
    Get-Context 'F3 pre-1.4 GMAS without GMC' $script:Out
    Assert-Contains 'F3 pre-1.4 GMAS without GMC' $script:Ctx @(
        'GMAS (GMC Ability System) at `Plugins/DeepWorlds_GMCAbilitySystem` (VersionName 1.3, pre-1.4',
        'GMC was not found under `Plugins/`',
        'gmas:gmas-upgrade')
    Assert-Exit0 'F3 pre-1.4 GMAS without GMC'

    # F4: GMC + 1.4 GMAS in a submodule-named folder, session started in a subdirectory.
    New-Dir "$Work/p4/Source/Game"; New-EmptyFile "$Work/p4/Game.uproject"
    New-Uplugin "$Work/p4/Plugins/GMC/GMC.uplugin" '2.3.9'
    New-Uplugin "$Work/p4/Plugins/GMCAbilitySystem/GMCAbilitySystem.uplugin" '1.4'
    New-EmptyFile "$Work/p4/Plugins/GMCAbilitySystem/Source/GMCAbilitySystem/Public/Utility/GMASBoundQueueV2.h"
    Invoke-Hook "$Work/p4/Source/Game"
    Get-Context 'F4 GMC + GMAS 1.4 from a subdirectory' $script:Out
    Assert-Contains 'F4 GMC + GMAS 1.4 from a subdirectory' $script:Ctx @(
        'GMC (General Movement Component) at `Plugins/GMC` (2.3.9)',
        'GMAS (GMC Ability System) at `Plugins/GMCAbilitySystem` (VersionName 1.4, 1.4+ bound queue V2)',
        '`Plugins/GMC/Source/GMCCore/Public/',
        'never copy GMC code',
        'gmas:gmas-rules', 'gmas:gmc-prediction', 'gmas:gmas-ability', 'gmas:gmas-effect', 'gmas:gmas-attribute',
        'gmas:gmas-task', 'gmas:gmas-debug', 'gmas:gmas-review', 'gmas:gmas-testing', 'gmas:gmas-setup', 'gmas:gmas-upgrade')
    Assert-Exit0 'F4 GMC + GMAS 1.4 from a subdirectory'

    # F5: a session inside a GMAS submodule of a project is the project's session, not the maintainer's.
    Invoke-Hook "$Work/p4/Plugins/GMCAbilitySystem/Source"
    Get-Context 'F5 inside the submodule' $script:Out
    Assert-Contains 'F5 inside the submodule: project context' $script:Ctx @('`Plugins/GMCAbilitySystem` (VersionName 1.4')
    if ($script:Ctx.Contains('gmas-maintain')) { Fail 'F5 inside the submodule: no maintainer context' $script:Ctx } else { Ok 'F5 inside the submodule: no maintainer context' }
    Assert-Exit0 'F5 inside the submodule'

    # F6: a standalone checkout of the GMAS repository -> maintainer context.
    New-Dir "$Work/repo/Source"; New-EmptyFile "$Work/repo/GMCAbilitySystem.uplugin"
    Invoke-Hook "$Work/repo/Source"
    Get-Context 'F6 GMAS repository checkout' $script:Out
    Assert-Contains 'F6 GMAS repository checkout' $script:Ctx @('GMAS plugin repository', 'gmas:gmas-maintain', 'generic')
    Assert-Exit0 'F6 GMAS repository checkout'

    # F7: disabled by environment -> silence.
    Invoke-Hook "$Work/p4" @{ CLAUDE_GMAS_HOOK_DISABLE = '1' }
    if (-not $script:Out) { Ok 'F7 disabled: silent' } else { Fail 'F7 disabled: silent' $script:Out }
    Assert-Exit0 'F7 disabled'

    # F8: output is a single line of valid JSON with the expected event name.
    Invoke-Hook "$Work/p4"
    if (@($script:Out -split "`n").Count -eq 1 -and $script:Out) { Ok 'F8 one line' } else { Fail 'F8 one line' $script:Out }
    try { $ev = [string](ConvertFrom-Json $script:Out).hookSpecificOutput.hookEventName } catch { $ev = '(unparsable JSON)' }
    if ($ev -eq 'SessionStart') { Ok 'F8 hookEventName' } else { Fail 'F8 hookEventName' $ev }
    Assert-Exit0 'F8 project with GMC and GMAS'

    # F9: the hooks.json command as Claude Code runs it on Windows without Git Bash: PowerShell
    # -Command, ${CLAUDE_PLUGIN_ROOT} rewritten to ${env:CLAUDE_PLUGIN_ROOT}, no bash on PATH.
    # Exactly one JSON line (the sh part must not run), nothing on stderr, exit 0.
    $onWindows = [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT
    if (-not $onWindows) {
        Ok 'F9 hooks.json under PowerShell: skipped (not Windows)'
    } else {
        $cmd = [string](ConvertFrom-Json ([System.IO.File]::ReadAllText((Join-Path $PluginRoot 'hooks/hooks.json')))).hooks.SessionStart[0].hooks[0].command
        $cmd = $cmd.Replace('${CLAUDE_PLUGIN_ROOT}', '${env:CLAUDE_PLUGIN_ROOT}')
        $psDir = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0'
        $env9 = @{ CLAUDE_GMAS_HOOK_DISABLE = $null; CLAUDE_GMAS_HOOK_DEBUG = $null; CLAUDE_PLUGIN_ROOT = $PluginRoot; PATH = $psDir }
        foreach ($case in @(@{ Name = 'project'; Dir = "$Work/p4/Source/Game"; Needle = '`Plugins/GMCAbilitySystem` (VersionName 1.4' },
                            @{ Name = 'repository'; Dir = "$Work/repo"; Needle = 'gmas:gmas-maintain' })) {
            $r = Invoke-Captured $PowerShellExe @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-Command', $cmd) $case.Dir $env9
            $out9 = $r.Out.TrimEnd("`r", "`n")
            $n = 'F9 hooks.json under PowerShell, ' + $case.Name
            if ($out9 -and @($out9 -split "`n").Count -eq 1) { Ok ($n + ': one line') } else { Fail ($n + ': one line') $out9 }
            Get-Context $n $out9
            Assert-Contains ($n + ': context') $script:Ctx @($case.Needle)
            if (-not $r.Err.Trim()) { Ok ($n + ': no stderr') } else { Fail ($n + ': no stderr') $r.Err }
            if ($r.Code -eq 0) { Ok ($n + ': exit 0') } else { Fail ($n + ': exit 0') ('exit ' + $r.Code) }
        }
    }
} finally {
    Remove-Item -Recurse -Force -LiteralPath $Work -ErrorAction SilentlyContinue
}

[Console]::Out.WriteLine(('{0} passed, {1} failed' -f $script:Pass, $script:Fail))
if ($script:Fail -eq 0) { exit 0 } else { exit 1 }
