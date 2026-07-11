[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Path,

    [Parameter(Mandatory = $true)]
    [ValidateSet('Win32', 'x64')]
    [string] $Architecture
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$resolvedPath = (Resolve-Path -LiteralPath $Path).Path
$bytes = [System.IO.File]::ReadAllBytes($resolvedPath)

if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
    throw "Not a valid PE image: $resolvedPath"
}

$peOffset = [System.BitConverter]::ToInt32($bytes, 0x3c)
if ($peOffset -lt 0 -or $peOffset + 6 -gt $bytes.Length) {
    throw "Invalid PE header offset in $resolvedPath"
}

if ($bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or
    $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) {
    throw "Missing PE signature in $resolvedPath"
}

$machine = [System.BitConverter]::ToUInt16($bytes, $peOffset + 4)
$expected = if ($Architecture -eq 'Win32') { [uint16] 0x014c } else { [uint16] 0x8664 }

if ($machine -ne $expected) {
    throw ('PE architecture mismatch for {0}: expected {1} (0x{2:X4}), found 0x{3:X4}' -f
           $resolvedPath, $Architecture, $expected, $machine)
}

Write-Host ('Verified {0}: {1} (0x{2:X4})' -f $resolvedPath, $Architecture, $machine)
