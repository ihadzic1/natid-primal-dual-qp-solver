param(
    [string]$NatIDSdkRoot = "$env:USERPROFILE\natID.SDK",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$BuildDirectory = "build"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$SdkCMake = Join-Path $NatIDSdkRoot "DevEnv\Common.cmake"

if (-not (Test-Path $SdkCMake -PathType Leaf)) {
    throw "natID SDK was not found at '$NatIDSdkRoot'."
}

cmake `
    -S $ProjectRoot `
    -B (Join-Path $ProjectRoot $BuildDirectory) `
    -G "Visual Studio 17 2022" `
    -A x64 `
    "-DNATID_SDK_ROOT=$NatIDSdkRoot"

cmake `
    --build (Join-Path $ProjectRoot $BuildDirectory) `
    --config $Configuration `
    --parallel

Write-Host ""
Write-Host "Build completed."
Write-Host "Before running from a terminal, add this directory to PATH:"
Write-Host "  $NatIDSdkRoot\bin"
