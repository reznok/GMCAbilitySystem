# Merge the DeepWorlds fork's dev branch into this repository's dev branch. PowerShell port of
# sync-deepworlds.sh (same behaviour, same environment variables, same gh calls) for maintainers
# on Windows; the scheduled workflow runs the bash script.
#
# Clean merge:  the merge commit is pushed to TARGET_BRANCH.
# Conflict:     the fork tip is force-pushed to SYNC_BRANCH and one pull request
#               (created or updated) carries the report for a human to resolve.
# Up to date:   nothing changes; a stale conflict PR is closed.
#
# Dry run from any full clone (needs no token):
#   $env:DRY_RUN = 'true'; powershell -NoProfile -ExecutionPolicy Bypass -File .github/scripts/sync-deepworlds.ps1
# Runs under Windows PowerShell 5.1 and PowerShell 7. Keep this file ASCII-only (5.1 reads ANSI).
# See docs/BRANCHING.md.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false } catch { }

function Get-Setting([string]$Name, [string]$Default) {
    $v = [System.Environment]::GetEnvironmentVariable($Name)
    if ($v) { return $v }
    return $Default
}

$UpstreamUrl = Get-Setting 'UPSTREAM_URL' 'https://github.com/DeepWorldsSA/DeepWorlds_GMCAbilitySystem.git'
$UpstreamBranch = Get-Setting 'UPSTREAM_BRANCH' 'dev'
$UpstreamRemote = Get-Setting 'UPSTREAM_REMOTE' 'deepworlds'
$TargetBranch = Get-Setting 'TARGET_BRANCH' 'dev'
$SyncBranch = Get-Setting 'SYNC_BRANCH' 'sync/deepworlds'
$CanonicalUrl = Get-Setting 'CANONICAL_URL' 'https://github.com/reznok/GMCAbilitySystem.git'
$DryRun = Get-Setting 'DRY_RUN' 'false'
$Label = 'deepworlds-sync'
$MaxListedCommits = 50
$Utf8 = New-Object System.Text.UTF8Encoding $false

function Write-Log([string]$Text) { [Console]::Error.Write($Text + "`n") }
function Stop-Script([string]$Text) { Write-Log ('error: ' + $Text); exit 1 }
function Test-Dry { return $DryRun -eq 'true' }

# One argument quoted for a Windows command line (C runtime rules; .NET on Unix splits the same way).
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

# The program and leading arguments that run <name>: an executable as is, a .ps1 through this
# PowerShell, a .cmd or .bat through cmd.exe.
$script:ToolCache = @{}
function Resolve-Tool([string]$Name) {
    if ($script:ToolCache.ContainsKey($Name)) { return $script:ToolCache[$Name] }
    $c = Get-Command $Name -CommandType Application, ExternalScript -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $c) { Stop-Script "$Name not found on PATH" }
    $ext = [System.IO.Path]::GetExtension($c.Source).ToLowerInvariant()
    if ($ext -eq '.ps1') {
        $self = [System.Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
        $tool = @{ File = $self; Lead = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $c.Source) }
    } elseif ($ext -eq '.cmd' -or $ext -eq '.bat') {
        $tool = @{ File = (Join-Path $env:SystemRoot 'System32\cmd.exe'); Lead = @('/d', '/c', $c.Source) }
    } else {
        $tool = @{ File = $c.Source; Lead = @() }
    }
    $script:ToolCache[$Name] = $tool
    return $tool
}

# Runs a tool in the current directory; returns Out (stdout without the final newline), Err, Code.
# Nothing is echoed: callers pass Err on (Invoke-Quiet) or keep it (captured calls).
function Invoke-Tool([string]$Name, [string[]]$Arguments) {
    $tool = Resolve-Tool $Name
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $tool.File
    $psi.Arguments = (@(@($tool.Lead) + @($Arguments) | ForEach-Object { ConvertTo-NativeArg $_ })) -join ' '
    $psi.WorkingDirectory = (Get-Location).ProviderPath
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = $Utf8
    $psi.StandardErrorEncoding = $Utf8
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.StandardInput.Close()
    $errTask = $p.StandardError.ReadToEndAsync()
    $out = $p.StandardOutput.ReadToEnd()
    $p.WaitForExit()
    return [pscustomobject]@{ Out = $out.TrimEnd("`r", "`n"); Err = $errTask.Result; Code = $p.ExitCode }
}

# git/gh call whose stdout is the result; stops the script when it fails (bash: set -e).
function Get-Output([string]$Name, [string[]]$Arguments) {
    $r = Invoke-Tool $Name $Arguments
    if ($r.Err) { [Console]::Error.Write($r.Err) }
    if ($r.Code -ne 0) { Stop-Script ("$Name $($Arguments -join ' ') failed (exit $($r.Code))") }
    return $r.Out
}
# git/gh call run for its effect: output passed through; returns $true on success.
function Invoke-Quiet([string]$Name, [string[]]$Arguments) {
    $r = Invoke-Tool $Name $Arguments
    if ($r.Out) { [Console]::Out.Write($r.Out + "`n") }
    if ($r.Err) { [Console]::Error.Write($r.Err) }
    return ($r.Code -eq 0)
}
# Same, and stops the script when it fails.
function Invoke-Checked([string]$Name, [string[]]$Arguments) {
    if (-not (Invoke-Quiet $Name $Arguments)) { Stop-Script ("$Name $($Arguments -join ' ') failed") }
}

function Get-Short([string]$Rev) { return Get-Output 'git' @('rev-parse', '--short', $Rev) }

# Markdown report: the Actions step summary when available, else stdout.
function Write-Report([string]$Text) {
    $summary = [System.Environment]::GetEnvironmentVariable('GITHUB_STEP_SUMMARY')
    if ($summary) { [System.IO.File]::AppendAllText($summary, $Text + "`n", $Utf8) }
    else { [Console]::Out.Write($Text + "`n") }
}

# owner/repo from the upstream URL, for messages.
function Get-UpstreamLabel {
    $parts = $UpstreamUrl.TrimEnd('/', '\') -split '[/\\]'
    $repo = $parts[$parts.Count - 1]
    if ($repo.EndsWith('.git')) { $repo = $repo.Substring(0, $repo.Length - 4) }
    $owner = ''
    if ($parts.Count -ge 2) { $owner = $parts[$parts.Count - 2] }
    return "$owner/$repo"
}

function Get-OpenPrNumber {
    return Get-Output 'gh' @('pr', 'list', '--head', $SyncBranch, '--base', $TargetBranch, '--state', 'open', '--json', 'number', '--jq', '.[0].number // empty')
}

# Close the conflict PR if one is open and delete the sync branch if present.
function Close-PrAndBranch([string]$Comment) {
    $number = Get-OpenPrNumber
    if ($number) { [void](Get-Output 'gh' @('pr', 'close', $number, '--comment', $Comment)) }
    if ((Invoke-Tool 'git' @('ls-remote', '--exit-code', '--heads', 'origin', $SyncBranch)).Code -eq 0) {
        Invoke-Checked 'git' @('push', '--quiet', 'origin', '--delete', $SyncBranch)
    }
}

function Get-Lines([string]$Text) {
    if (-not $Text) { return , @() }
    return , @($Text -split "`r?`n" | Where-Object { $_ -ne '' })
}

# Sets Base, Count, CommitsMd, OverlapMd for HEAD..upstream.
function Read-Range {
    $r = Invoke-Tool 'git' @('merge-base', 'HEAD', $script:Upstream)
    if ($r.Code -ne 0) { Stop-Script 'no common history with the fork' }
    $script:Base = $r.Out
    $script:Count = [int](Get-Output 'git' @('rev-list', '--count', "$($script:Base)..$($script:Upstream)"))
    $script:CommitsMd = Get-Output 'git' @('log', '-n', "$MaxListedCommits", '--format=- %h %ad %an %s', '--date=short', "$($script:Base)..$($script:Upstream)")
    if ($script:Count -gt $MaxListedCommits) {
        $script:CommitsMd += "`n- " + [char]0x2026 + ' and ' + ($script:Count - $MaxListedCommits) + ' more'
    }
    $local = Get-Lines (Get-Output 'git' @('diff', '--name-only', $script:Base, 'HEAD'))
    $fork = Get-Lines (Get-Output 'git' @('diff', '--name-only', $script:Base, $script:Upstream))
    $set = New-Object 'System.Collections.Generic.HashSet[string]' -ArgumentList (, [string[]]$fork)
    $both = New-Object System.Collections.Generic.List[string]
    foreach ($f in $local) { if ($set.Contains($f) -and -not $both.Contains($f)) { $both.Add($f) } }
    $both.Sort([System.StringComparer]::Ordinal)
    if ($both.Count -gt 0) { $script:OverlapMd = (@($both | ForEach-Object { '- ' + $_ })) -join "`n" } else { $script:OverlapMd = '- none' }
}

function Get-MergeMessage {
    $b = Get-Short $script:Base
    $u = Get-Short $script:Upstream
    return ('Sync DeepWorlds {0}: {1} commits ({2}..{3})' -f $UpstreamBranch, $script:Count, $b, $u) + "`n`n" +
        ('Merges {0} {1} into {2}.' -f (Get-UpstreamLabel), $UpstreamBranch, $TargetBranch) + "`n`n" +
        "Commits:`n" + $script:CommitsMd + "`n`n" +
        ('Overlap with local changes since {0}:' -f $b) + "`n" + $script:OverlapMd + "`n"
}

function Write-ReportRange([string]$Heading) {
    Write-Report $Heading
    Write-Report $script:CommitsMd
    Write-Report ''
    Write-Report ('Overlap with local changes since ' + (Get-Short $script:Base) + ':')
    Write-Report $script:OverlapMd
}

function Get-PrBody {
    $b = Get-Short $script:Base
    $u = Get-Short $script:Upstream
    $lines = @(
        ('Automated merge of {0} `{1}` ({2}..{3}) into `{4}` failed.' -f (Get-UpstreamLabel), $UpstreamBranch, $b, $u, $TargetBranch),
        '',
        '## Conflicting files',
        $script:ConflictsMd,
        '',
        ('## Overlap with local changes since {0}' -f $b),
        $script:OverlapMd,
        '',
        '## Commits',
        $script:CommitsMd,
        '',
        '## Resolve locally',
        ('    git fetch origin {0} {1}' -f $TargetBranch, $SyncBranch),
        ('    git checkout {0}' -f $TargetBranch),
        '    git pull --ff-only',
        ('    git merge --no-ff origin/{0}' -f $SyncBranch),
        '    # resolve conflicts, build locally, then',
        ('    git push origin {0}' -f $TargetBranch),
        '',
        ('Do not merge this pull request on GitHub: it carries the unresolved fork tip. The next scheduled run closes it and deletes `{0}` once `{1}` contains {2}.' -f $SyncBranch, $TargetBranch, $u)
    )
    return $lines -join "`n"
}

# ---- preconditions
$current = Get-Output 'git' @('rev-parse', '--abbrev-ref', 'HEAD')
if ($current -ne $TargetBranch) { Stop-Script "on '$current'; check out '$TargetBranch' first" }
if (Get-Output 'git' @('status', '--porcelain')) { Stop-Script 'working tree is not clean' }
if ((Get-Output 'git' @('rev-parse', '--is-shallow-repository')) -ne 'false') { Stop-Script 'shallow clone; full history is required' }
if (-not (Test-Dry) -and -not $env:GH_TOKEN) { Stop-Script 'GH_TOKEN is required unless DRY_RUN=true' }
$originUrl = (Invoke-Tool 'git' @('remote', 'get-url', 'origin')).Out
$canonicalBare = $CanonicalUrl
if ($canonicalBare.EndsWith('.git')) { $canonicalBare = $canonicalBare.Substring(0, $canonicalBare.Length - 4) }
if (@($CanonicalUrl, $canonicalBare, ($canonicalBare + '.git')) -cnotcontains $originUrl) {
    if (-not (Test-Dry)) { Stop-Script "origin is '$originUrl', not the canonical repository ($CanonicalUrl); set CANONICAL_URL to override" }
}

# ---- fetch the fork
if ((Invoke-Tool 'git' @('remote', 'get-url', $UpstreamRemote)).Code -eq 0) {
    Invoke-Checked 'git' @('remote', 'set-url', $UpstreamRemote, $UpstreamUrl)
} else {
    Invoke-Checked 'git' @('remote', 'add', $UpstreamRemote, $UpstreamUrl)
}
Invoke-Checked 'git' @('fetch', '--quiet', $UpstreamRemote, $UpstreamBranch)
$script:Upstream = Get-Output 'git' @('rev-parse', 'FETCH_HEAD')

# ---- merge, pushing at most twice: a push rejected because TARGET_BRANCH moved
# refetches it and redoes the merge once from the new tip.
$attempt = 1
while ($true) {
    if ((Invoke-Tool 'git' @('merge-base', '--is-ancestor', $script:Upstream, 'HEAD')).Code -eq 0) {
        $tip = Get-Short $script:Upstream
        Write-Report '## Sync DeepWorlds: up to date'
        Write-Report ('`{0}` already contains the fork tip `{1}`.' -f $TargetBranch, $tip)
        if (-not (Test-Dry)) { Close-PrAndBranch ('`{0}` already contains the fork tip `{1}`; superseded.' -f $TargetBranch, $tip) }
        exit 0
    }

    Read-Range

    $merge = Invoke-Tool 'git' @('merge', '--no-ff', '--no-edit', '-m', (Get-MergeMessage), $script:Upstream)
    if ($merge.Code -eq 0) {
        if (Test-Dry) {
            Invoke-Checked 'git' @('reset', '--quiet', '--hard', 'ORIG_HEAD')
            Write-ReportRange ('## Sync DeepWorlds: dry run, would merge {0} commits ({1}..{2})' -f $script:Count, (Get-Short $script:Base), (Get-Short $script:Upstream))
            exit 0
        }
        $mergeSha = Get-Short 'HEAD'
        if (Invoke-Quiet 'git' @('push', '--quiet', 'origin', "HEAD:$TargetBranch")) {
            Write-ReportRange ('## Sync DeepWorlds: merged {0} commits in `{1}`' -f $script:Count, $mergeSha)
            if ($attempt -ne 1) { Write-Report "(push succeeded on attempt $attempt)" }
            Close-PrAndBranch ('Merged cleanly in `{0}`.' -f $mergeSha)
            exit 0
        }
        if ($attempt -ge 2) { Stop-Script "push to $TargetBranch rejected twice" }
        Write-Log "push rejected; refetching $TargetBranch and retrying"
        $attempt++
        Invoke-Checked 'git' @('fetch', '--quiet', 'origin', $TargetBranch)
        Invoke-Checked 'git' @('reset', '--quiet', '--hard', 'FETCH_HEAD')
        continue
    }

    # ---- conflict
    $conflicts = Get-Lines (Get-Output 'git' @('diff', '--name-only', '--diff-filter=U'))
    [void](Invoke-Tool 'git' @('merge', '--abort'))
    if ($conflicts.Count -eq 0) { Stop-Script ('merge failed without conflicts: ' + ($merge.Out + "`n" + $merge.Err).Trim()) }
    $conflictCount = $conflicts.Count
    $script:ConflictsMd = (@($conflicts | ForEach-Object { '- ' + $_ })) -join "`n"

    if (Test-Dry) {
        Write-Report "## Sync DeepWorlds: dry run, $($script:Count) commits conflict in $conflictCount files"
        Write-Report $script:ConflictsMd
        Write-Report ''
        Write-ReportRange 'Commits:'
        exit 0
    }

    Invoke-Checked 'git' @('push', '--quiet', '--force', 'origin', "$($script:Upstream):refs/heads/$SyncBranch")
    [void](Get-Output 'gh' @('label', 'create', $Label, '--color', 'C5DEF5', '--description', 'Automated fork sync', '--force'))
    $title = "Sync DeepWorlds ${UpstreamBranch}: $($script:Count) commits, conflicts in $conflictCount files"
    $body = Get-PrBody
    $number = Get-OpenPrNumber
    if ($number) {
        [void](Get-Output 'gh' @('pr', 'edit', $number, '--title', $title, '--body', $body))
        Write-Report "## Sync DeepWorlds: conflicts in $conflictCount files; pull request updated"
    } else {
        [void](Get-Output 'gh' @('pr', 'create', '--head', $SyncBranch, '--base', $TargetBranch, '--title', $title, '--body', $body, '--label', $Label))
        Write-Report "## Sync DeepWorlds: conflicts in $conflictCount files; pull request created"
    }
    Write-Report $script:ConflictsMd
    exit 0
}
