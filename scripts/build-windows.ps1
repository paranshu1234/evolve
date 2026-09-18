param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw 'Install Visual Studio 2022 Desktop development with C++, including CMake and Windows SDK, then run from Developer PowerShell.'
    }
    cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    cmake --build build --config $Configuration
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    ctest --test-dir build -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Core tests failed.' }
    Write-Host "Built: $root\build\$Configuration\Evolve.exe"
} finally { Pop-Location }
