# Packs Fall Desktop as an MSIX for the Microsoft Store. The package is unsigned:
# the Store signs it after certification.
#   .\installer\msix\pack.ps1 -Version 1.2.3 -Exe build\Release\falldesktop.exe
# Output: dist-store\FallDesktop-<version>-x64.msix, plus its unpacked layout in
# build\msix. To try a package with Developer Mode on, register a copy of that
# layout ("Add-AppxPackage -Register <copy>\AppxManifest.xml"): every pack rebuilds build\msix.
param(
    [Parameter(Mandatory)] [string]$Version,
    [Parameter(Mandatory)] [string]$Exe,
    [string]$OutDir,  # default dist-store
    [string]$Layout   # default build\msix
)
$ErrorActionPreference = 'Stop'
# The Store's package versions: the first number can't be 0, and none can exceed 65535.
if ($Version -notmatch '^[1-9]\d*\.\d+\.\d+$' -or ($Version.Split('.') | Where-Object { [long]$_ -gt 65535 })) {
    throw "Version must look like 1.2.3, starting above 0 and with no number above 65535 (got '$Version')."
}

# Absolute file-system paths throughout: .NET and the SDK tools don't know PowerShell's location.
$full = { param([string]$Path) $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path) }
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot '..\..\dist-store' }
if (-not $Layout) { $Layout = Join-Path $PSScriptRoot '..\..\build\msix' }
$Exe = & $full $Exe
$OutDir = & $full $OutDir
$Layout = & $full $Layout

# MakePri and MakeAppx from the newest Windows SDK that has both.
$bin = Get-ChildItem -LiteralPath (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin') -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -as [version] } |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64' } |
    Where-Object { (Test-Path (Join-Path $_ 'makeappx.exe')) -and (Test-Path (Join-Path $_ 'makepri.exe')) } |
    Select-Object -First 1
if (-not $bin) { throw 'MakeAppx.exe and MakePri.exe were not found: install the Windows 10/11 SDK.' }

function Invoke-Tool([string]$Tool, [string[]]$Arguments) {
    $ErrorActionPreference = 'Continue'  # stderr is diagnostics (Windows PowerShell 5.1 would stop on it)
    $encoding = [Console]::OutputEncoding
    try {
        if ($Tool -eq 'makepri.exe') { [Console]::OutputEncoding = [System.Text.Encoding]::Unicode }  # it writes UTF-16 when redirected
        $output = & (Join-Path $bin $Tool) @Arguments 2>&1
    } finally {
        [Console]::OutputEncoding = $encoding
    }
    if ($LASTEXITCODE) {
        $output | ForEach-Object { "$_" } | Write-Host
        throw "$Tool failed with exit code $LASTEXITCODE."
    }
}

$identity = Import-PowerShellDataFile (Join-Path $PSScriptRoot 'identity.psd1')
if (-not $identity.Name -or -not $identity.Publisher) {
    Write-Warning ('installer\msix\identity.psd1 has no Partner Center identity yet: packing with a ' +
                   'local test identity. The Store will not accept this package.')
    $identity.Name = 'FallDesktop.Test'
    $identity.Publisher = 'CN=FallDesktopTest'
    if (-not $identity.PublisherDisplayName) { $identity.PublisherDisplayName = 'Joshua Starr' }
} elseif (-not $identity.PublisherDisplayName) {
    throw 'installer\msix\identity.psd1 has a Name and Publisher but no PublisherDisplayName: copy it from Partner Center too.'
}

# The layout is rebuilt from scratch. Never delete anything that isn't a layout,
# or a layout registered as a development package (Fall Desktop may be running from it).
if (Test-Path -LiteralPath $Layout) {
    if ((Get-ChildItem -LiteralPath $Layout -Force | Select-Object -First 1) -and
        -not (Test-Path -LiteralPath (Join-Path $Layout 'AppxManifest.xml'))) {
        throw "$Layout is not an MSIX layout: not deleting it."
    }
    $registered = try { Get-AppxPackage | Where-Object { $_.InstallLocation -eq $Layout } } catch { $null }
    if ($registered) {
        throw ("$Layout is registered as $($registered.Name). Close Fall Desktop and run " +
               "'Get-AppxPackage $($registered.Name) | Remove-AppxPackage' first.")
    }
    Remove-Item -LiteralPath $Layout -Recurse -Force
}

# The exe, the manifest, the logos and MakePri's index of the logos.
New-Item -ItemType Directory -Force -Path (Join-Path $Layout 'Assets') | Out-Null
Copy-Item -LiteralPath $Exe -Destination (Join-Path $Layout 'falldesktop.exe')
Copy-Item -Path (Join-Path $PSScriptRoot 'Assets\*.png') -Destination (Join-Path $Layout 'Assets')

$xml = { param([string]$Text) [System.Security.SecurityElement]::Escape($Text) }
$manifest = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $PSScriptRoot 'AppxManifest.xml')
$manifest = $manifest.Replace('$(Name)', (& $xml $identity.Name))
$manifest = $manifest.Replace('$(Publisher)', (& $xml $identity.Publisher))
$manifest = $manifest.Replace('$(PublisherDisplayName)', (& $xml $identity.PublisherDisplayName))
$manifest = $manifest.Replace('$(Version)', "$Version.0")  # the Store reserves the fourth number: always 0
$manifestPath = Join-Path $Layout 'AppxManifest.xml'
[System.IO.File]::WriteAllText($manifestPath, $manifest, (New-Object System.Text.UTF8Encoding $false))

# Index the scale and target-size variants of the logos so Windows can pick them. The
# config stays outside the layout, or MakeAppx would pack it.
$priConfig = Join-Path ([System.IO.Path]::GetTempPath()) "FallDesktop-priconfig-$PID.xml"
try {
    Invoke-Tool 'makepri.exe' @('createconfig', '/cf', $priConfig, '/dq', 'en-US', '/pv', '10.0.0', '/o')
    # One resources.pri with every variant: the default config would move the other
    # scales into split files meant for the resource packages of an app bundle.
    $config = New-Object System.Xml.XmlDocument
    $config.Load($priConfig)
    $packaging = $config.SelectSingleNode('/resources/packaging')
    if ($packaging) { $null = $config.DocumentElement.RemoveChild($packaging) }
    $config.Save($priConfig)
    Invoke-Tool 'makepri.exe' @('new', '/pr', $Layout, '/cf', $priConfig, '/mn', $manifestPath,
                                '/of', (Join-Path $Layout 'resources.pri'), '/o')
} finally {
    if (Test-Path -LiteralPath $priConfig) { Remove-Item -LiteralPath $priConfig }
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$msix = Join-Path $OutDir "FallDesktop-$Version-x64.msix"
Invoke-Tool 'makeappx.exe' @('pack', '/d', $Layout, '/p', $msix, '/o')
Write-Host "Packed: $msix"
