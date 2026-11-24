# 독립 실행으로 자동 점검 투어를 돌린다. 화면은 실행 위치의 Saved/M2, Saved/M3에 저장된다.
# run_tour.ps1 [-Tour TwinTour|TwinTourM3] [-Offline] [-Package <패키지 폴더>]
param([string]$Tour = 'TwinTourM3', [switch]$Offline, [string]$Package)
$root = Split-Path $PSScriptRoot
if (Get-NetTCPConnection -LocalPort 8765 -State Listen -ErrorAction Ignore) { throw '8765 포트를 쓰는 에뮬레이터를 먼저 끄세요' }
$common = '-windowed', '-ResX=1920', '-ResY=1080', '-ForceRes', '-novsync', "-$Tour", '-nosound', '-log=Tour.log', '-ExecCmds="t.MaxFPS 0"'
if ($Package) {
    $exe = Join-Path $Package 'MetaTwin.exe'; $saved = Join-Path $Package 'MetaTwin\Saved'; $gameArgs = $common
} else {
    $exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'; $saved = "$root\Saved"
    $gameArgs = @("`"$root\MetaTwin.uproject`"", '-game', '-ModelContextProtocolPort=8012') + $common
}
$emu = if (-not $Offline) { Start-Process python -ArgumentList "`"$root\Tools\emulator\emulator.py`"" -PassThru -WindowStyle Hidden }
try {
    $game = Start-Process $exe -PassThru -ArgumentList $gameArgs
    if (-not $game.WaitForExit(300000)) { $game.Kill($true); 'TOUR TIMEOUT' }
} finally { if ($emu -and -not $emu.HasExited) { $emu.Kill() } }
Get-ChildItem "$saved\Logs" -Filter '*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1 | ForEach-Object {
    $_.FullName; Select-String -Path $_.FullName -Pattern 'LogTwinTour|LogTwinTelemetry|Fatal error|Ensure condition' | ForEach-Object Line }
