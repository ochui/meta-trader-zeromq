[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Tag,

    [Parameter(Mandatory = $true)]
    [string] $Mt4PackageDirectory,

    [Parameter(Mandatory = $true)]
    [string] $Mt5PackageDirectory,

    [Parameter(Mandatory = $true)]
    [string] $OutputDirectory,

    [string] $RepositoryRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$temporaryBase = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())

if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    $RepositoryRoot = Split-Path -Parent $PSScriptRoot
}

function Remove-TemporaryDirectory {
    param([string] $Path)

    $resolved = [System.IO.Path]::GetFullPath($Path)
    $prefix = $temporaryBase.TrimEnd([System.IO.Path]::DirectorySeparatorChar) +
        [System.IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove staging directory outside the temporary directory: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

if ($Tag -notmatch '^v(?<version>[0-9]+\.[0-9]+\.[0-9]+)(?:-[0-9A-Za-z.-]+)?$') {
    throw "Tag must use vX.Y.Z or vX.Y.Z-prerelease format: $Tag"
}
$tagVersion = $Matches.version

$repository = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$cmakePath = Join-Path $repository 'CMakeLists.txt'
$cmake = Get-Content -LiteralPath $cmakePath -Raw
$versionMatch = [regex]::Match(
    $cmake,
    'project\s*\(\s*meta_trader_zeromq\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)',
    [System.Text.RegularExpressions.RegexOptions]::IgnoreCase
)
if (-not $versionMatch.Success) {
    throw "Could not read the project version from $cmakePath"
}
$projectVersion = $versionMatch.Groups[1].Value
if ($tagVersion -ne $projectVersion) {
    throw "Tag version $tagVersion does not match CMake project version $projectVersion"
}

function Resolve-Package {
    param([string] $Directory, [string] $Name)

    $resolved = (Resolve-Path -LiteralPath $Directory).Path
    foreach ($relative in @(
        'zmq_bind.dll',
        'libzmq.dll',
        'include/zmq_bind.mqh',
        'include/zmq_native.mqh',
        'include/zmq_bind.h'
    )) {
        $path = Join-Path $resolved $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Missing $Name package file: $path"
        }
    }
    return $resolved
}

$mt4Package = Resolve-Package $Mt4PackageDirectory 'MT4 Win32'
$mt5Package = Resolve-Package $Mt5PackageDirectory 'MT5 x64'
$readme = Join-Path $repository 'README.md'
if (-not (Test-Path -LiteralPath $readme -PathType Leaf)) {
    throw "Missing release README: $readme"
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$output = (Resolve-Path -LiteralPath $OutputDirectory).Path
$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("mt-zmq-release-" + [guid]::NewGuid().ToString('N'))
$mt4Stage = Join-Path $temporaryRoot 'mt4'
$mt5Stage = Join-Path $temporaryRoot 'mt5'
$headersStage = Join-Path $temporaryRoot 'headers'

try {
    foreach ($directory in @($mt4Stage, $mt5Stage, (Join-Path $headersStage 'mql'), (Join-Path $headersStage 'native'))) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }

    Copy-Item -Path (Join-Path $mt4Package '*') -Destination $mt4Stage -Recurse -Force
    Copy-Item -Path (Join-Path $mt5Package '*') -Destination $mt5Stage -Recurse -Force
    Copy-Item -LiteralPath $readme -Destination (Join-Path $mt4Stage 'README.md')
    Copy-Item -LiteralPath $readme -Destination (Join-Path $mt5Stage 'README.md')

    Copy-Item -LiteralPath (Join-Path $mt4Package 'include/zmq_bind.mqh') `
        -Destination (Join-Path $headersStage 'mql/zmq_bind.mqh')
    Copy-Item -LiteralPath (Join-Path $mt4Package 'include/zmq_native.mqh') `
        -Destination (Join-Path $headersStage 'mql/zmq_native.mqh')
    Copy-Item -LiteralPath (Join-Path $mt4Package 'include/zmq_bind.h') `
        -Destination (Join-Path $headersStage 'native/zmq_bind.h')
    Copy-Item -LiteralPath $readme -Destination (Join-Path $headersStage 'README.md')

    $assets = @(
        @{ Name = "meta-trader-zeromq-$Tag-mt4-win32.zip"; Source = $mt4Stage },
        @{ Name = "meta-trader-zeromq-$Tag-mt5-x64.zip"; Source = $mt5Stage },
        @{ Name = "meta-trader-zeromq-$Tag-headers.zip"; Source = $headersStage }
    )

    foreach ($asset in $assets) {
        $destination = Join-Path $output $asset.Name
        if (Test-Path -LiteralPath $destination) {
            Remove-Item -LiteralPath $destination -Force
        }
        Compress-Archive -Path (Join-Path $asset.Source '*') -DestinationPath $destination
    }

    $checksumLines = foreach ($asset in $assets) {
        $path = Join-Path $output $asset.Name
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $($asset.Name)"
    }
    $checksumPath = Join-Path $output 'SHA256SUMS.txt'
    Set-Content -LiteralPath $checksumPath -Value $checksumLines -Encoding ascii

    Write-Host "Validated release version $projectVersion for tag $Tag"
    foreach ($asset in $assets) {
        Write-Host "Created $($asset.Name)"
    }
    Write-Host 'Created SHA256SUMS.txt'
}
finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-TemporaryDirectory $temporaryRoot
    }
}
