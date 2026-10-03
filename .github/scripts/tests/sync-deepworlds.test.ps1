# Tests for sync-deepworlds.ps1 (PowerShell port of sync-deepworlds.test.sh, same cases T1 to T8).
# Builds throwaway repositories under a temp dir and stubs `gh` with a gh.ps1 shim first on PATH;
# no network, no GitHub. Needs git (Git for Windows runs T6's pre-receive hook with its own sh).
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File .github/scripts/tests/sync-deepworlds.test.ps1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$SyncScript = [System.IO.Path]::GetFullPath((Join-Path $Here '../sync-deepworlds.ps1'))
$PowerShellExe = [System.Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
$Tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('gmas-sync-test-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $Tmp)
$Utf8 = New-Object System.Text.UTF8Encoding $false

# Environment for every git and sync call in this process and its children.
$saved = @{}
function Set-TestEnv([string]$Name, $Value) {
    if (-not $saved.ContainsKey($Name)) { $saved[$Name] = [System.Environment]::GetEnvironmentVariable($Name) }
    [System.Environment]::SetEnvironmentVariable($Name, $Value)
}
[System.IO.File]::WriteAllText((Join-Path $Tmp 'empty.gitconfig'), '')
Set-TestEnv 'GIT_AUTHOR_NAME' 'test'; Set-TestEnv 'GIT_AUTHOR_EMAIL' 'test@example.com'
Set-TestEnv 'GIT_COMMITTER_NAME' 'test'; Set-TestEnv 'GIT_COMMITTER_EMAIL' 'test@example.com'
Set-TestEnv 'GIT_CONFIG_GLOBAL' (Join-Path $Tmp 'empty.gitconfig'); Set-TestEnv 'GIT_CONFIG_NOSYSTEM' '1'
Set-TestEnv 'GH_TOKEN' 'fake'
Set-TestEnv 'GITHUB_STEP_SUMMARY' $null
Set-TestEnv 'DRY_RUN' $null
Set-TestEnv 'GH_LOG' (Join-Path $Tmp 'gh.log')
Set-TestEnv 'GH_PR_LIST_JSON' '[]'

# gh shim: logs every call, answers `pr list` from GH_PR_LIST_JSON. With --jq it emulates the
# script's one query (first PR number, or nothing).
[void](New-Item -ItemType Directory -Path (Join-Path $Tmp 'bin'))
$shim = @'
[System.IO.File]::AppendAllText($env:GH_LOG, ($args -join ' ') + "`n")
if ($args.Count -ge 2 -and $args[0] -eq 'pr' -and $args[1] -eq 'list') {
    if ($args -contains '--jq') {
        $m = [regex]::Match($env:GH_PR_LIST_JSON, '"number": *([0-9]+)')
        if ($m.Success) { [Console]::Out.WriteLine($m.Groups[1].Value) }
    } else { [Console]::Out.WriteLine($env:GH_PR_LIST_JSON) }
} elseif ($args.Count -ge 2 -and $args[0] -eq 'pr' -and $args[1] -eq 'create') {
    [Console]::Out.WriteLine('https://example.invalid/pull/1')
}
exit 0
'@
[System.IO.File]::WriteAllText((Join-Path $Tmp 'bin/gh.ps1'), $shim)
Set-TestEnv 'PATH' ((Join-Path $Tmp 'bin') + [System.IO.Path]::PathSeparator + $env:PATH)

$script:PassCount = 0
$script:FailCount = 0
function Ok([string]$Label) { $script:PassCount++; [Console]::Out.WriteLine('ok   ' + $Label) }
function Bad([string]$Label) { $script:FailCount++; [Console]::Out.WriteLine('FAIL ' + $Label) }
function Assert-Contains([string]$Hay, [string]$Needle, [string]$Label) {
    if ($Hay.Contains($Needle)) { Ok $Label }
    else { Bad ($Label + ' (missing: ' + $Needle + ')'); foreach ($l in ($Hay -split "`r?`n")) { [Console]::Out.WriteLine('     | ' + $l) } }
}
function Assert-NotContains([string]$Hay, [string]$Needle, [string]$Label) {
    if ($Hay.Contains($Needle)) { Bad ($Label + ' (unexpected: ' + $Needle + ')') } else { Ok $Label }
}
function Assert-Eq([string]$Got, [string]$Want, [string]$Label) {
    if ($Got -ceq $Want) { Ok $Label } else { Bad ($Label + " (got '" + $Got + "', want '" + $Want + "')") }
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
    $psi.StandardOutputEncoding = $Utf8
    $psi.StandardErrorEncoding = $Utf8
    if ($Env) { foreach ($k in $Env.Keys) { $psi.EnvironmentVariables[$k] = [string]$Env[$k] } }
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.StandardInput.Close()
    $errTask = $p.StandardError.ReadToEndAsync()
    $out = $p.StandardOutput.ReadToEnd()
    $p.WaitForExit()
    return [pscustomobject]@{ Out = $out; Err = $errTask.Result; Code = $p.ExitCode }
}

# git in <dir>; returns stdout without the final newline; throws on failure unless -AllowFail.
function G([string]$Dir, [string[]]$GitArgs, [switch]$AllowFail) {
    $r = Invoke-Captured 'git' $GitArgs $Dir $null
    if ($r.Code -ne 0 -and -not $AllowFail) { throw ('git ' + ($GitArgs -join ' ') + ' failed in ' + $Dir + ': ' + $r.Err) }
    return $r.Out.TrimEnd("`r", "`n")
}

# sed -i on one file: a line regex replacement; content stays LF.
function Edit-File([string]$Path, [string]$Pattern, [string]$Replacement) {
    $t = [System.IO.File]::ReadAllText($Path)
    [System.IO.File]::WriteAllText($Path, [regex]::Replace($t, $Pattern, $Replacement, [System.Text.RegularExpressions.RegexOptions]::Multiline), $Utf8)
}

# A fixture: canonical bare repo (origin), fork bare repo, a work clone of the canonical dev
# branch, and a fork clone to make fork commits in. shared.txt has 20 numbered lines so both
# sides can edit different lines.
function New-Fixture([string]$Name) {
    $d = Join-Path $Tmp $Name
    [void](New-Item -ItemType Directory -Path "$d/seed")
    [void](G $d @('init', '-q', '--bare', '-b', 'dev', "$d/canonical.git"))
    [void](G "$d/seed" @('init', '-q', '-b', 'dev'))
    [System.IO.File]::WriteAllText("$d/seed/shared.txt", ((@(1..20 | ForEach-Object { "line $_" }) -join "`n") + "`n"), $Utf8)
    [System.IO.File]::WriteAllText("$d/seed/file.txt", "base`n", $Utf8)
    [void](G "$d/seed" @('add', '.'))
    [void](G "$d/seed" @('commit', '-q', '-m', 'base'))
    [void](G "$d/seed" @('push', '-q', "$d/canonical.git", 'dev'))
    [void](G $d @('clone', '-q', '--bare', "$d/canonical.git", "$d/fork.git"))
    [void](G $d @('clone', '-q', '-b', 'dev', "$d/canonical.git", "$d/work"))
    # The origin URL exactly as git recorded it.
    [System.IO.File]::WriteAllText("$d/canonical.url", (G "$d/work" @('remote', 'get-url', 'origin')), $Utf8)
    [void](G $d @('clone', '-q', '-b', 'dev', "$d/fork.git", "$d/forkwork"))
    return $d
}
function Edit-Fork([string]$D, [string]$File, [string]$Pattern, [string]$Replacement, [string]$Message) {
    Edit-File "$D/forkwork/$File" $Pattern $Replacement
    [void](G "$D/forkwork" @('commit', '-qam', $Message))
    [void](G "$D/forkwork" @('push', '-q', 'origin', 'dev'))
}
function Edit-Local([string]$D, [string]$File, [string]$Pattern, [string]$Replacement, [string]$Message) {
    Edit-File "$D/work/$File" $Pattern $Replacement
    [void](G "$D/work" @('commit', '-qam', $Message))
    [void](G "$D/work" @('push', '-q', 'origin', 'dev'))
}
# Runs the sync script in <fixture>/work: stdout then stderr, plus "[exit N]" when it failed.
function Invoke-Sync([string]$D, [hashtable]$Extra = @{}) {
    $vars = @{ UPSTREAM_URL = "$D/fork.git"; CANONICAL_URL = [System.IO.File]::ReadAllText("$D/canonical.url") }
    foreach ($k in $Extra.Keys) { $vars[$k] = $Extra[$k] }
    $r = Invoke-Captured $PowerShellExe @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $SyncScript) "$D/work" $vars
    $out = $r.Out + $r.Err
    if ($r.Code -ne 0) { $out += "`n[exit " + $r.Code + ']' }
    return $out
}
function Get-SyncBranchSha([string]$D) { return (G "$D/canonical.git" @('rev-parse', '--verify', '-q', 'refs/heads/sync/deepworlds') -AllowFail) }
function Reset-GhLog { [System.IO.File]::WriteAllText($env:GH_LOG, '') }
function Get-GhLog { return [System.IO.File]::ReadAllText($env:GH_LOG) }

try {
    # ---- T1: up to date
    Reset-GhLog
    $d = New-Fixture 't1'
    $before = G "$d/work" @('rev-parse', 'HEAD')
    $out = Invoke-Sync $d
    Assert-Contains $out 'up to date' 'T1 reports up to date'
    Assert-Eq (G "$d/work" @('rev-parse', 'HEAD')) $before 'T1 HEAD unchanged'
    Assert-Contains (Get-GhLog) 'pr list' 'T1 looks for a stale conflict PR'

    # ---- T2: clean merge, dry run (fork edits line 20 and file.txt, local edits line 1: overlap, no conflict)
    Reset-GhLog
    $d = New-Fixture 't2'
    Edit-Local $d 'shared.txt' '^line 1$' 'line 1 local' 'local: line 1'
    Edit-Fork $d 'shared.txt' '^line 20$' 'line 20 fork' 'fork: line 20'
    Edit-Fork $d 'file.txt' '^base$' 'base fork' 'fork: file'
    $before = G "$d/work" @('rev-parse', 'HEAD')
    $out = Invoke-Sync $d @{ DRY_RUN = 'true' }
    Assert-Contains $out 'would merge 2 commits' 'T2 dry run counts fork commits'
    Assert-Contains $out '- shared.txt' 'T2 overlap lists shared.txt'
    Assert-NotContains $out '- file.txt' 'T2 overlap excludes the fork-only file'
    Assert-Eq (G "$d/work" @('rev-parse', 'HEAD')) $before 'T2 HEAD unchanged after dry run'
    Assert-Eq (G "$d/work" @('status', '--porcelain')) '' 'T2 tree clean after dry run'

    # ---- T3: clean merge, live
    Reset-GhLog
    $out = Invoke-Sync $d
    Assert-Contains $out 'merged 2 commits' 'T3 reports the merge'
    Assert-Contains (G "$d/canonical.git" @('log', '-1', '--format=%s', 'dev')) 'Sync DeepWorlds dev: 2 commits (' 'T3 merge commit pushed to canonical dev'
    $body = G "$d/canonical.git" @('log', '-1', '--format=%b', 'dev')
    Assert-Contains $body 'Overlap with local changes since' 'T3 merge body has the overlap section'
    Assert-Contains $body '- shared.txt' 'T3 merge body lists the overlap'
    Assert-Contains $body 'fork: line 20' 'T3 merge body lists fork commits'
    Assert-NotContains (Get-GhLog) 'pr create' 'T3 opens no PR'
    Assert-Eq (Get-SyncBranchSha $d) '' 'T3 leaves no sync branch'

    # ---- T4: conflict, dry run (both sides edit line 1)
    Reset-GhLog
    $d = New-Fixture 't4'
    Edit-Local $d 'shared.txt' '^line 1$' 'line 1 local' 'local: line 1'
    Edit-Fork $d 'shared.txt' '^line 1$' 'line 1 fork' 'fork: line 1'
    $out = Invoke-Sync $d @{ DRY_RUN = 'true' }
    Assert-Contains $out 'conflict in 1 files' 'T4 dry run reports the conflict'
    Assert-Contains $out '- shared.txt' 'T4 lists the conflicting file'
    Assert-Eq (G "$d/work" @('status', '--porcelain')) '' 'T4 tree clean after abort'
    Assert-Eq (Get-SyncBranchSha $d) '' 'T4 dry run pushes no sync branch'

    # ---- T5a: conflict, live: branch pushed, PR created
    Reset-GhLog
    $out = Invoke-Sync $d
    Assert-Contains $out 'conflicts in 1 files; pull request created' 'T5a reports the PR'
    $forktip = G "$d/fork.git" @('rev-parse', 'dev')
    Assert-Eq (Get-SyncBranchSha $d) $forktip 'T5a sync branch equals the fork tip'
    $ghlog = Get-GhLog
    Assert-Contains $ghlog 'label create deepworlds-sync' 'T5a ensures the label'
    Assert-Contains $ghlog 'pr create --head sync/deepworlds --base dev --title Sync DeepWorlds dev: 1 commits, conflicts in 1 files' 'T5a creates the PR with the title'
    Assert-Contains $ghlog '## Conflicting files' 'T5a PR body has the conflict section'
    Assert-Contains $ghlog 'git merge --no-ff origin/sync/deepworlds' 'T5a PR body has the resolve recipe'

    # ---- T5b: conflict persists and a PR exists: edited, not created
    Reset-GhLog
    $out = Invoke-Sync $d @{ GH_PR_LIST_JSON = '[{"number":7}]' }
    $ghlog = Get-GhLog
    Assert-Contains $ghlog 'pr edit 7 --title' 'T5b edits the existing PR'
    Assert-NotContains $ghlog 'pr create' 'T5b creates no second PR'
    Assert-Contains $out 'pull request updated' 'T5b reports the update'

    # ---- T5c: resolved by hand and pushed: the next run closes the PR and deletes the branch
    [void](G "$d/work" @('merge', '-q', '--no-ff', '-X', 'ours', '-m', 'resolve', $forktip))
    [void](G "$d/work" @('push', '-q', 'origin', 'dev'))
    Reset-GhLog
    $out = Invoke-Sync $d @{ GH_PR_LIST_JSON = '[{"number":7}]' }
    Assert-Contains $out 'up to date' 'T5c up to date after the manual resolve'
    Assert-Contains (Get-GhLog) 'pr close 7 --comment' 'T5c closes the PR'
    Assert-Eq (Get-SyncBranchSha $d) '' 'T5c deletes the sync branch'

    # ---- T6: push rejected once (a pre-receive hook rejects the first push after arming);
    # a stale conflict PR and sync branch exist and must be cleaned up after the clean merge
    Reset-GhLog
    $d = New-Fixture 't6'
    Edit-Fork $d 'file.txt' '^base$' 'base fork' 'fork: file'
    [void](G "$d/work" @('push', '-q', 'origin', 'HEAD:refs/heads/sync/deepworlds'))
    $hook = "#!/usr/bin/env bash`nif [ -f `"`$GIT_DIR/reject-once`" ]; then rm -f `"`$GIT_DIR/reject-once`"; echo `"rejected once`" >&2; exit 1; fi`nexit 0`n"
    [System.IO.File]::WriteAllText("$d/canonical.git/hooks/pre-receive", $hook, $Utf8)
    if (-not ([System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT)) { [void](Invoke-Captured 'chmod' @('+x', "$d/canonical.git/hooks/pre-receive") $d $null) }
    [System.IO.File]::WriteAllText("$d/canonical.git/reject-once", '')
    $out = Invoke-Sync $d @{ GH_PR_LIST_JSON = '[{"number":9}]' }
    Assert-Contains $out 'push rejected; refetching' 'T6 logs the retry'
    Assert-Contains $out 'merged 1 commits' 'T6 merges after the retry'
    Assert-Contains $out 'attempt 2' 'T6 report mentions the second attempt'
    Assert-Contains (G "$d/canonical.git" @('log', '-1', '--format=%s', 'dev')) 'Sync DeepWorlds dev: 1 commits' 'T6 canonical dev advanced'
    Assert-Contains (Get-GhLog) 'pr close 9 --comment' 'T6 closes the stale PR after the clean merge'
    Assert-Eq (Get-SyncBranchSha $d) '' 'T6 deletes the stale sync branch'

    # ---- T7: a real non-fast-forward race: canonical dev moves after work was cloned,
    # so the first push is rejected; the retry refetches dev and merges from the new tip
    Reset-GhLog
    $d = New-Fixture 't7'
    Edit-Fork $d 'file.txt' '^base$' 'base fork' 'fork: file'
    [void](G $d @('clone', '-q', '-b', 'dev', "$d/canonical.git", "$d/other"))
    Edit-File "$d/other/shared.txt" '^line 10$' 'line 10 other'
    [void](G "$d/other" @('commit', '-qam', 'other: line 10'))
    [void](G "$d/other" @('push', '-q', 'origin', 'dev'))
    $moved = G "$d/canonical.git" @('rev-parse', 'dev')
    $out = Invoke-Sync $d
    Assert-Contains $out 'push rejected; refetching' 'T7 first push rejected (non-fast-forward)'
    Assert-Contains $out 'attempt 2' 'T7 merged on the second attempt'
    $isAncestor = (Invoke-Captured 'git' @('merge-base', '--is-ancestor', $moved, 'dev') "$d/canonical.git" $null).Code -eq 0
    $yes = ''; if ($isAncestor) { $yes = 'yes' }
    Assert-Eq $yes 'yes' 'T7 canonical dev keeps the concurrent commit'
    Assert-Contains (G "$d/canonical.git" @('log', '-1', '--format=%s', 'dev')) 'Sync DeepWorlds dev: 1 commits' 'T7 canonical dev ends on the sync merge'

    # ---- T8: a clone whose origin is not the canonical repository refuses a live run
    # (a dry run still works, so fixtures and forks can preview)
    Reset-GhLog
    $d = New-Fixture 't8'
    Edit-Fork $d 'file.txt' '^base$' 'base fork' 'fork: file'
    [void](G $d @('init', '-q', '--bare', '-b', 'dev', "$d/other.git"))
    [void](G "$d/work" @('remote', 'set-url', 'origin', "$d/other.git"))
    $out = Invoke-Sync $d @{ DRY_RUN = 'false' }
    Assert-Contains $out 'not the canonical repository' 'T8 refuses a non-canonical origin'
    Assert-Contains $out '[exit 1]' 'T8 exits non-zero'
    $out = Invoke-Sync $d @{ DRY_RUN = 'true' }
    Assert-NotContains $out 'not the canonical repository' 'T8 dry run is not refused'
} finally {
    foreach ($k in $saved.Keys) { [System.Environment]::SetEnvironmentVariable($k, $saved[$k]) }
    Remove-Item -Recurse -Force -LiteralPath $Tmp -ErrorAction SilentlyContinue
}

[Console]::Out.WriteLine('')
[Console]::Out.WriteLine(('{0} passed, {1} failed' -f $script:PassCount, $script:FailCount))
if ($script:FailCount -eq 0) { exit 0 } else { exit 1 }
