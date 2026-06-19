<#
.SYNOPSIS
  实机回归测试驱动器 (FH6 FocusKeeper farm 流水线)。

.DESCRIPTION
  通过 debug 命令通道 (build/debug/cmd.txt) 驱动正在运行的 debug 版 FocusKeeper,
  逐条执行 cases.json 里的用例:写命令 -> 等「流水线结束」日志里程碑 -> 校验
  expect/forbid 正则 + status.txt 结构化断言 + 操作员肉眼复核,最后产出报告。

  仅适用于 `make farm` 的 DEBUG 版 (命令通道只在 FK_DEBUG 下存在)。

.PARAMETER Only
  只跑指定用例 (按 name,可多个),例如: -Only TC-READ,TC-RACE

.PARAMETER CasesFile
  用例清单路径,默认同目录 cases.json

.EXAMPLE
  pwsh tests/regression/run.ps1
  pwsh tests/regression/run.ps1 -Only TC-RACE
#>
[CmdletBinding()]
param(
    [string[]] $Only,
    [string]   $CasesFile = ''
)

$ErrorActionPreference = 'Stop'
# $PSScriptRoot is not populated in param defaults, so resolve paths here.
$ScriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $CasesFile) { $CasesFile = Join-Path $ScriptDir 'cases.json' }
$RepoRoot = (Resolve-Path (Join-Path $ScriptDir '..\..')).Path
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Resolve-RepoPath([string]$rel) { Join-Path $RepoRoot $rel }

function Write-Cmd([string]$cmdPath, [string]$verb) {
    [System.IO.File]::WriteAllText($cmdPath, $verb, $Utf8NoBom)
}

function Read-Text([string]$path) {
    if (-not (Test-Path $path)) { return $null }
    for ($i = 0; $i -lt 5; $i++) {
        try { return [System.IO.File]::ReadAllText($path) } catch { Start-Sleep -Milliseconds 120 }
    }
    return $null
}

# 日志被运行中的程序持有写句柄,必须用共享读打开。
function Read-LogText([string]$path) {
    if (-not (Test-Path $path)) { return '' }
    for ($i = 0; $i -lt 5; $i++) {
        try {
            $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::Open,
                  [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
            try {
                $sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8)
                return $sr.ReadToEnd()
            } finally { $fs.Dispose() }
        } catch { Start-Sleep -Milliseconds 150 }
    }
    return ''
}

# 写一个命令并等 cmd.ack 出现(确认程序已拾取)。返回 ack 文本或 $null。
function Send-Command([hashtable]$P, [string]$verb, [int]$ackTimeoutMs = 6000) {
    if (Test-Path $P.ack) { Remove-Item $P.ack -Force -ErrorAction SilentlyContinue }
    Write-Cmd $P.cmd $verb
    $deadline = (Get-Date).AddMilliseconds($ackTimeoutMs)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 300
        if (Test-Path $P.ack) { return (Read-Text $P.ack).Trim() }
    }
    return $null
}

# 触发一次 status 命令,解析 status.txt 成 hashtable。
function Get-PipeStatus([hashtable]$P) {
    if (Test-Path $P.status) { Remove-Item $P.status -Force -ErrorAction SilentlyContinue }
    Write-Cmd $P.cmd 'status'
    $deadline = (Get-Date).AddMilliseconds(4000)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 250
        if (Test-Path $P.status) {
            $h = @{}
            foreach ($line in (Read-Text $P.status) -split "`n") {
                if ($line -match '^\s*(\w+)\s*=\s*(.*?)\s*$') { $h[$Matches[1]] = $Matches[2] }
            }
            if ($h.Count -gt 0) { return $h }
        }
    }
    return $null
}

function Test-StatusAssert([hashtable]$status, [string]$expr) {
    if ($expr -notmatch '^\s*(\w+)\s*(==|!=|>=|<=|>|<)\s*(-?\d+)\s*$') {
        return @{ ok = $false; why = "断言语法无效: $expr" }
    }
    $key = $Matches[1]; $op = $Matches[2]; $rhs = [int]$Matches[3]
    if ($null -eq $status -or -not $status.ContainsKey($key)) {
        return @{ ok = $false; why = "status 缺少字段 '$key'" }
    }
    $lhs = 0
    if (-not [int]::TryParse($status[$key], [ref]$lhs)) {
        return @{ ok = $false; why = "字段 '$key'=$($status[$key]) 非整数" }
    }
    $pass = switch ($op) {
        '==' { $lhs -eq $rhs } '!=' { $lhs -ne $rhs }
        '>=' { $lhs -ge $rhs } '<=' { $lhs -le $rhs }
        '>'  { $lhs -gt $rhs } '<'  { $lhs -lt $rhs }
    }
    return @{ ok = $pass; why = "$key=$lhs $op $rhs" }
}

# ----------------------------------------------------------------------------

if (-not (Test-Path $CasesFile)) { throw "找不到用例文件: $CasesFile" }
$spec = Get-Content $CasesFile -Raw -Encoding UTF8 | ConvertFrom-Json

$P = @{
    log    = Resolve-RepoPath $spec.paths.log
    cmd    = Resolve-RepoPath $spec.paths.cmd
    ack    = Resolve-RepoPath $spec.paths.ack
    status = Resolve-RepoPath $spec.paths.status
}
$doneMarker = $spec.done_marker

Write-Host "=== FH6 FocusKeeper 实机回归 ===" -ForegroundColor Cyan
Write-Host "仓库根 : $RepoRoot"
Write-Host "日志   : $($P.log)"
Write-Host "命令   : $($P.cmd)"

if (-not (Get-Process FocusKeeper -ErrorAction SilentlyContinue)) {
    Write-Host "`n[!] 没检测到 FocusKeeper 进程。请先启动 build\FocusKeeper.exe (debug 版,点 UAC)。" -ForegroundColor Yellow
    Read-Host "启动好后按回车继续 (Ctrl+C 退出)"
}

$cases = $spec.cases
if ($Only) { $cases = $cases | Where-Object { $Only -contains $_.name } }
if (-not $cases) { throw "没有匹配的用例 (检查 -Only 参数)" }

$results = New-Object System.Collections.ArrayList

foreach ($c in $cases) {
    Write-Host "`n--------------------------------------------------------" -ForegroundColor DarkGray
    Write-Host "[$($c.name)] $($c.desc)" -ForegroundColor Cyan
    Write-Host "前置: $($c.setup)"
    $ans = Read-Host "命令『$($c.cmd)』。回车开始,输入 s 跳过"
    if ($ans -eq 's') {
        [void]$results.Add([pscustomobject]@{ Name=$c.name; Result='SKIP'; Reason='操作员跳过' })
        continue
    }

    # 1) 记录日志当前长度(只断言本次追加部分)
    $prevLen = (Read-LogText $P.log).Length

    # 2) 发命令并确认拾取
    $ack = Send-Command $P $c.cmd
    if (-not $ack) {
        [void]$results.Add([pscustomobject]@{ Name=$c.name; Result='FAIL'; Reason='命令未被拾取(程序在运行吗?)' })
        continue
    }
    if ($ack -match 'unknown') {
        [void]$results.Add([pscustomobject]@{ Name=$c.name; Result='FAIL'; Reason="未知命令: $ack" })
        continue
    }
    Write-Host "  已拾取: $ack" -ForegroundColor DarkGray

    # 3) 轮询日志,等「流水线结束」里程碑或超时
    $deadline = (Get-Date).AddSeconds([int]$c.timeout_s)
    $appended = ''
    $done = $false
    Write-Host "  运行中 (超时 $($c.timeout_s)s)..." -NoNewline
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 2
        Write-Host "." -NoNewline
        $full = Read-LogText $P.log
        $appended = if ($full.Length -ge $prevLen) { $full.Substring($prevLen) } else { $full }
        if ($appended -match [regex]::Escape($doneMarker)) { $done = $true; break }
    }
    Write-Host ""

    $fails = New-Object System.Collections.ArrayList
    if (-not $done) { [void]$fails.Add("超时未见『$doneMarker』") }

    # 4) expect / forbid
    foreach ($pat in $c.expect) {
        if ($appended -notmatch $pat) { [void]$fails.Add("缺少 expect: /$pat/") }
    }
    foreach ($pat in $c.forbid) {
        if ($appended -match $pat) { [void]$fails.Add("命中 forbid: /$pat/") }
    }

    # 5) status 断言
    if ($c.status_assert -and $c.status_assert.Count -gt 0) {
        $st = Get-PipeStatus $P
        foreach ($expr in $c.status_assert) {
            $r = Test-StatusAssert $st $expr
            if (-not $r.ok) { [void]$fails.Add("status 断言失败: $($r.why)") }
        }
    }

    # 6) 肉眼复核
    if ($c.manual) {
        $m = Read-Host "  [复核] $($c.manual) (y/n)"
        if ($m -notmatch '^(y|Y)') { [void]$fails.Add("肉眼复核未通过: $($c.manual)") }
    }

    if ($fails.Count -eq 0) {
        Write-Host "  PASS" -ForegroundColor Green
        [void]$results.Add([pscustomobject]@{ Name=$c.name; Result='PASS'; Reason='' })
    } else {
        Write-Host "  FAIL" -ForegroundColor Red
        $fails | ForEach-Object { Write-Host "    - $_" -ForegroundColor Red }
        [void]$results.Add([pscustomobject]@{ Name=$c.name; Result='FAIL'; Reason=($fails -join '; ') })
    }
}

# ----------------------------------------------------------------------------
Write-Host "`n================ 汇总 ================" -ForegroundColor Cyan
$results | Format-Table -AutoSize | Out-String | Write-Host
$pass = @($results | Where-Object Result -eq 'PASS').Count
$fail = @($results | Where-Object Result -eq 'FAIL').Count
$skip = @($results | Where-Object Result -eq 'SKIP').Count
Write-Host "PASS=$pass FAIL=$fail SKIP=$skip" -ForegroundColor $(if ($fail) { 'Red' } else { 'Green' })

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$reportPath = Join-Path $ScriptDir "report_$stamp.md"
$md = New-Object System.Collections.ArrayList
[void]$md.Add("# 回归报告 $stamp")
[void]$md.Add("")
[void]$md.Add("PASS=$pass FAIL=$fail SKIP=$skip")
[void]$md.Add("")
[void]$md.Add("| 用例 | 结果 | 说明 |")
[void]$md.Add("|------|------|------|")
foreach ($r in $results) { [void]$md.Add("| $($r.Name) | $($r.Result) | $($r.Reason) |") }
[System.IO.File]::WriteAllText($reportPath, ($md -join "`n"), $Utf8NoBom)
Write-Host "报告已写入: $reportPath"

exit $(if ($fail) { 1 } else { 0 })
