# Windows 패키지를 만든다. package.ps1 [-Config Shipping|Development]
param([ValidateSet('Shipping', 'Development')][string]$Config = 'Shipping')
$root = Split-Path $PSScriptRoot
$out = "$root\Build\Package\$Config"
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat' BuildCookRun "-project=$root\MetaTwin.uproject" -noP4 -platform=Win64 "-clientconfig=$Config" -build -cook -stage -pak -iostore -compressed -prereqs -archive "-archivedirectory=$out" -utf8output -unattended
exit $LASTEXITCODE
