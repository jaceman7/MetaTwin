# Windows PowerShell 5.1의 WinRT 변환기로 영상을 줄인다. compress_video.ps1 <입력> <출력> [bps]
param([string]$In, [string]$Out, [uint32]$Bitrate = 12000000)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
$null = [Windows.Media.Transcoding.MediaTranscoder, Windows.Media.Transcoding, ContentType = WindowsRuntime]
$null = [Windows.Media.MediaProperties.MediaEncodingProfile, Windows.Media.MediaProperties, ContentType = WindowsRuntime]
$m = [System.WindowsRuntimeSystemExtensions].GetMethods()
$asOp = $m | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
$asAct = $m | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncActionWithProgress`1' } | Select-Object -First 1
function Wait-Op($op, $type) { $t = $asOp.MakeGenericMethod($type).Invoke($null, @($op)); $t.Wait(); $t.Result }
$src = Wait-Op ([Windows.Storage.StorageFile]::GetFileFromPathAsync((Resolve-Path $In).Path)) ([Windows.Storage.StorageFile])
$dir = Wait-Op ([Windows.Storage.StorageFolder, Windows.Storage, ContentType = WindowsRuntime]::GetFolderFromPathAsync((Resolve-Path (Split-Path $Out)).Path)) ([Windows.Storage.StorageFolder])
$dst = Wait-Op ($dir.CreateFileAsync((Split-Path $Out -Leaf), [Windows.Storage.CreationCollisionOption]::ReplaceExisting)) ([Windows.Storage.StorageFile])
$p = [Windows.Media.MediaProperties.MediaEncodingProfile]::CreateMp4([Windows.Media.MediaProperties.VideoEncodingQuality]::HD1080p)
$p.Video.Bitrate = $Bitrate; $p.Video.FrameRate.Numerator = 30; $p.Video.FrameRate.Denominator = 1; $p.Audio = $null
$t = New-Object Windows.Media.Transcoding.MediaTranscoder
$r = Wait-Op ($t.PrepareFileTranscodeAsync($src, $dst, $p)) ([Windows.Media.Transcoding.PrepareTranscodeResult, Windows.Media.Transcoding, ContentType = WindowsRuntime])
if (-not $r.CanTranscode) { throw "cannot transcode: $($r.FailureReason)" }
$asAct.MakeGenericMethod([double]).Invoke($null, @($r.TranscodeAsync())).Wait()
