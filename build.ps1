# Builds falldesktop.exe (and optionally the installer and the Store package) with CMake + Visual Studio 2022.
#   .\build.ps1                       # Release build -> build\Release\falldesktop.exe
#   .\build.ps1 -Installer            # ...and dist\FallDesktop-<version>-setup.exe (needs Inno Setup 6)
#   .\build.ps1 -Msix                 # ...and dist-store\FallDesktop-<version>-x64.msix (Microsoft Store)
#   .\build.ps1 -Version 1.2.0 -Installer -Msix
#   .\build.ps1 -Config Debug
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Config = 'Release',
    [string]$Version = '',
    [switch]$Installer,
    [switch]$Msix
)
$ErrorActionPreference = 'Stop'
if ($Msix -and $Config -ne 'Release') { throw '-Msix packs Release builds only: the Store rejects debug builds.' }
$root = $PSScriptRoot
$build = Join-Path $root 'build'

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'CMake was not found on PATH. Install it from https://cmake.org or via Visual Studio.'
}

# Shipped builds never carry the developer test hooks, even if this build tree was configured with them.
$configure = @('-S', $root, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DFALLDESKTOP_DEV_TOOLS=OFF')
if ($Version) { $configure += "-DFALLDESKTOP_VERSION=$Version" }
cmake @configure
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build --config $Config --parallel
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$exe = Join-Path $build "$Config\falldesktop.exe"
Write-Host "`nBuilt: $exe"
if (($Installer -or $Msix) -and -not $Version) {
    $Version = (Select-String -Path (Join-Path $build 'CMakeCache.txt') -Pattern '^FALLDESKTOP_VERSION:STRING=(.+)$').Matches[0].Groups[1].Value
}

if ($Installer) {
    $iscc = @(
        (Get-Command iscc -ErrorAction SilentlyContinue).Source,
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
    ) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $iscc) { throw 'Inno Setup 6 was not found (winget install JRSoftware.InnoSetup).' }
    & $iscc "/DAppVersion=$Version" "/DSourceExe=$exe" (Join-Path $root 'installer\falldesktop.iss')
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

if ($Msix) {
    & (Join-Path $root 'installer\msix\pack.ps1') -Version $Version -Exe $exe
}
