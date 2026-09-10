param(
    [string]$NatIdSdkRoot = "$env:USERPROFILE\natID.SDK",
    [string]$NatIdUtilsRoot = "$env:USERPROFILE\natID.Utils"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$ramDiskBacking = "$env:USERPROFILE\natID.RAMDisk"
$buildRoot = "R:\Out"
$setupOutputRoot = "R:\Setup"
$collectorSource = "$env:USERPROFILE\Desktop\NatIDQP-Packaging-Source"
$collectorConfigs = "$env:USERPROFILE\NatIDQP.Setups"
$gtkPackageFile = Join-Path $NatIdSdkRoot "DevEnv\SetupCollectors\Packages\GTK4.xml"
$gtkBackupFile = "$env:TEMP\NatIDQP-GTK4-backup.xml"
$createdCollectorJunction = $false
$hasGtkBackup = $false

function Assert-LastCommand([string]$message) {
    if ($LASTEXITCODE -ne 0) {
        throw $message
    }
}

if (-not $IsWindows -and $PSVersionTable.PSEdition -eq "Core") {
    throw "This script must be run on Windows."
}
if (-not (Test-Path "$NatIdSdkRoot\DevEnv\Common.cmake")) {
    throw "natID SDK was not found at '$NatIdSdkRoot'."
}
if (-not (Test-Path "$NatIdUtilsRoot\windows\SetupCollector.exe")) {
    throw "SetupCollector.exe was not found under '$NatIdUtilsRoot'."
}

$env:HOME = $env:USERPROFILE
New-Item -ItemType Directory -Force -Path $ramDiskBacking | Out-Null
if (-not (Get-PSDrive -Name R -ErrorAction SilentlyContinue)) {
    cmd /c "subst R: `"$ramDiskBacking`""
    Assert-LastCommand "Could not create the R: RAMDisk mapping."
}

try {
    New-Item -ItemType Directory -Force -Path "$env:USERPROFILE\Desktop" | Out-Null
    if (Test-Path $collectorSource) {
        throw "Temporary collector path already exists: '$collectorSource'. Remove it or rename it before retrying."
    }
    New-Item -ItemType Junction -Path $collectorSource -Target $projectRoot | Out-Null
    $createdCollectorJunction = $true

    $devResFile = Join-Path $collectorSource "gui\res\DevRes.xml"
    if (-not (Test-Path $devResFile)) {
        throw "natID GUI resource descriptor was not found at '$devResFile'."
    }

    cmake -S $collectorSource -B $buildRoot -A x64 `
        "-DNATID_SDK_ROOT=$NatIdSdkRoot" `
        "-DBUILD_TESTING=ON"
    Assert-LastCommand "CMake configuration failed."

    cmake --build $buildRoot --config Release --target natid_qp_gui natid_qp_tests
    Assert-LastCommand "Release build failed."

    $env:PATH = "$NatIdSdkRoot\bin;$NatIdSdkRoot\bin\GTK;$env:PATH"
    ctest --test-dir $buildRoot -C Release --output-on-failure
    Assert-LastCommand "NatIDQP tests failed."

    $guiExecutable = Join-Path $buildRoot "NatIDQP\Release\natid_qp_gui.exe"
    if (-not (Test-Path $guiExecutable)) {
        throw "Expected GUI executable was not produced at '$guiExecutable'."
    }

    New-Item -ItemType Directory -Force -Path $collectorConfigs | Out-Null
    Copy-Item "$NatIdSdkRoot\DevEnv\SetupCollectors\*" $collectorConfigs -Recurse -Force
    Copy-Item "$projectRoot\packaging\NatIDQP.xml" "$collectorConfigs\NatIDQP.xml" -Force
    Copy-Item "$projectRoot\packaging\modSolver.xml" `
        "$collectorConfigs\Packages\modSolver.xml" -Force

    if (Test-Path $gtkPackageFile) {
        Copy-Item $gtkPackageFile $gtkBackupFile -Force
        $hasGtkBackup = $true
    }
    Copy-Item "$projectRoot\packaging\GTK4.xml" $gtkPackageFile -Force

    & "$NatIdUtilsRoot\windows\SetupCollector.exe" "$collectorConfigs\NatIDQP.xml"
    Assert-LastCommand "natID SetupCollector failed."

    $dist = Join-Path $projectRoot "installer-output"
    New-Item -ItemType Directory -Force -Path $dist | Out-Null
    Get-ChildItem $setupOutputRoot -File -Recurse |
        Where-Object { $_.Extension -eq ".msi" -or $_.Name -like "Install*.exe" } |
        Copy-Item -Destination $dist -Force

    $msiFiles = @(Get-ChildItem $dist -File -Filter *.msi)
    if ($msiFiles.Count -eq 0) {
        throw "No MSI was produced by SetupCollector."
    }

    $bootstrapperFiles = @(Get-ChildItem $dist -File -Filter "Install*.exe")
    $zipFile = Join-Path $projectRoot "NatIDQP-Windows-Installer.zip"
    $msiPaths = @($msiFiles | ForEach-Object { $_.FullName })

    try {
        if ($bootstrapperFiles.Count -eq 0) {
            throw "The optional installer bootstrapper EXE is unavailable."
        }

        $archiveInputs = @($msiPaths)
        $archiveInputs += @($bootstrapperFiles | ForEach-Object { $_.FullName })
        Compress-Archive -Path $archiveInputs -DestinationPath $zipFile -Force
        Write-Host "Installer EXE and MSI archive created: $zipFile"
    }
    catch {
        Write-Warning "Windows Security blocked packaging the unsigned natID bootstrapper EXE."
        Write-Warning "Creating a safe MSI-only archive instead. Do not disable antivirus protection."

        if (Test-Path $zipFile) {
            Remove-Item $zipFile -Force
        }
        Compress-Archive -Path $msiPaths -DestinationPath $zipFile -Force
        Write-Host "MSI-only installer archive created: $zipFile"
    }
}
finally {
    if ($hasGtkBackup) {
        Copy-Item $gtkBackupFile $gtkPackageFile -Force
    }
    if ($createdCollectorJunction -and (Test-Path $collectorSource)) {
        cmd /c "rmdir `"$collectorSource`""
    }
}
