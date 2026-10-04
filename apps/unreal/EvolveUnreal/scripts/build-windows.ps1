param(
    [Parameter(Mandatory=$true)][string]$EngineRoot,
    [switch]$Package,
    [string]$ArchiveDirectory = ""
)
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectRoot "EvolveUnreal.uproject"
$Build = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$UAT = Join-Path $EngineRoot "Engine\Build\BatchFiles\RunUAT.bat"
foreach ($Path in @($Build, $Editor, $UAT)) {
    if (!(Test-Path $Path)) { throw "Required installed Unreal tool missing: $Path" }
}
# Uses the existing engine/toolchain; never installs prerequisites or accepts agreements.
& $Build EvolveUnrealEditor Win64 Development "-Project=$Project" -WaitMutex
if ($LASTEXITCODE -ne 0) { throw "Unreal Editor target build failed: $LASTEXITCODE" }
$AssetLog = Join-Path $ProjectRoot "Saved\PrepareAssets.log"
New-Item -ItemType Directory -Force (Split-Path $AssetLog -Parent) | Out-Null
& $Editor $Project "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'prepare_assets.py')" -unattended -nosplash -nullrhi "-abslog=$AssetLog"
if ($LASTEXITCODE -ne 0) { throw "Asset generation failed: $LASTEXITCODE" }
if (!(Select-String -Path $AssetLog -Pattern "EVOLVE_ASSETS_READY" -Quiet)) { throw "Asset generation did not finish; inspect $AssetLog" }
& $Build EvolveUnreal Win64 Development "-Project=$Project" -WaitMutex
if ($LASTEXITCODE -ne 0) { throw "Unreal standalone target build failed: $LASTEXITCODE" }
if ($Package) {
    if (!$ArchiveDirectory) { $ArchiveDirectory = Join-Path $ProjectRoot "Packages" }
    & $UAT BuildCookRun "-project=$Project" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive "-archivedirectory=$ArchiveDirectory" -utf8output -unattended
    if ($LASTEXITCODE -ne 0) { throw "Unreal packaging failed: $LASTEXITCODE" }
}
Write-Host "Build completed. For a standalone editor-hosted window:"
Write-Host "& '$Editor' '$Project' -game -windowed -ResX=1440 -ResY=900"
