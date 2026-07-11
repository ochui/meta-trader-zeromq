[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Win32PeerDirectory,

    [Parameter(Mandatory = $true)]
    [string] $X64PeerDirectory,

    [int] $ReadyTimeoutSeconds = 10,
    [int] $TestTimeoutSeconds = 15,
    [int] $Win32SubscriberPort = 45731,
    [int] $X64SubscriberPort = 45732
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$temporaryBase = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())

function Remove-LogDirectory {
    param([string] $Path)

    $resolved = [System.IO.Path]::GetFullPath($Path)
    $prefix = $temporaryBase.TrimEnd([System.IO.Path]::DirectorySeparatorChar) +
        [System.IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove log directory outside the temporary directory: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

function Resolve-PeerDirectory {
    param([string] $Directory)

    $resolved = (Resolve-Path -LiteralPath $Directory).Path
    foreach ($file in @('zmq_cross_arch_peer.exe', 'zmq_bind.dll', 'libzmq.dll')) {
        $path = Join-Path $resolved $file
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Missing cross-architecture peer file: $path"
        }
    }
    return $resolved
}

function Read-Log {
    param([string] $Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return ''
    }
    return (Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue)
}

function Stop-PeerProcess {
    param($Process)

    if ($null -ne $Process) {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
            $Process.WaitForExit(5000) | Out-Null
        }
    }
}

function Wait-ForReady {
    param($Process, [string] $OutputPath)

    $deadline = [DateTime]::UtcNow.AddSeconds($ReadyTimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "Subscriber exited before reporting READY (exit code $($Process.ExitCode))"
        }
        if ((Read-Log $OutputPath) -match '(?m)^READY\r?$') {
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Subscriber did not report READY within $ReadyTimeoutSeconds seconds"
}

function Wait-ForPeers {
    param($Subscriber, $Publisher)

    $deadline = [DateTime]::UtcNow.AddSeconds($TestTimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Subscriber.Refresh()
        $Publisher.Refresh()
        if ($Subscriber.HasExited -and $Publisher.HasExited) {
            $Subscriber.WaitForExit()
            $Publisher.WaitForExit()
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Cross-architecture peers did not exit within $TestTimeoutSeconds seconds"
}

function Invoke-CrossArchitectureDirection {
    param(
        [string] $Name,
        [string] $SubscriberDirectory,
        [string] $PublisherDirectory,
        [int] $Port,
        [string] $Message
    )

    $logRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("mt-zmq-cross-arch-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $logRoot | Out-Null
    $subscriberOut = Join-Path $logRoot 'subscriber.out.log'
    $subscriberErr = Join-Path $logRoot 'subscriber.err.log'
    $publisherOut = Join-Path $logRoot 'publisher.out.log'
    $publisherErr = Join-Path $logRoot 'publisher.err.log'
    $subscriber = $null
    $publisher = $null
    $failure = $null
    $endpoint = "tcp://127.0.0.1:$Port"

    try {
        $subscriber = Start-Process `
            -FilePath (Join-Path $SubscriberDirectory 'zmq_cross_arch_peer.exe') `
            -ArgumentList @('subscriber', $endpoint, $Message) `
            -WorkingDirectory $SubscriberDirectory `
            -RedirectStandardOutput $subscriberOut `
            -RedirectStandardError $subscriberErr `
            -WindowStyle Hidden `
            -PassThru
        $null = $subscriber.Handle

        Wait-ForReady $subscriber $subscriberOut

        $publisher = Start-Process `
            -FilePath (Join-Path $PublisherDirectory 'zmq_cross_arch_peer.exe') `
            -ArgumentList @('publisher', $endpoint, $Message) `
            -WorkingDirectory $PublisherDirectory `
            -RedirectStandardOutput $publisherOut `
            -RedirectStandardError $publisherErr `
            -WindowStyle Hidden `
            -PassThru
        $null = $publisher.Handle

        Wait-ForPeers $subscriber $publisher
        if ($subscriber.ExitCode -ne 0 -or $publisher.ExitCode -ne 0) {
            throw "Peer exit codes: subscriber=$($subscriber.ExitCode), publisher=$($publisher.ExitCode)"
        }
    }
    catch {
        $failure = $_.Exception.Message
    }
    finally {
        Stop-PeerProcess $publisher
        Stop-PeerProcess $subscriber
    }

    if ($null -ne $failure) {
        Write-Host "--- $Name subscriber stdout ---"
        Write-Host (Read-Log $subscriberOut)
        Write-Host "--- $Name subscriber stderr ---"
        Write-Host (Read-Log $subscriberErr)
        Write-Host "--- $Name publisher stdout ---"
        Write-Host (Read-Log $publisherOut)
        Write-Host "--- $Name publisher stderr ---"
        Write-Host (Read-Log $publisherErr)
        Remove-LogDirectory $logRoot
        throw "$Name failed: $failure"
    }

    Write-Host "PASS: $Name"
    Remove-LogDirectory $logRoot
}

$win32Peer = Resolve-PeerDirectory $Win32PeerDirectory
$x64Peer = Resolve-PeerDirectory $X64PeerDirectory

Invoke-CrossArchitectureDirection `
    -Name 'Win32 subscriber <- x64 publisher' `
    -SubscriberDirectory $win32Peer `
    -PublisherDirectory $x64Peer `
    -Port $Win32SubscriberPort `
    -Message 'x64-to-win32'

Invoke-CrossArchitectureDirection `
    -Name 'x64 subscriber <- Win32 publisher' `
    -SubscriberDirectory $x64Peer `
    -PublisherDirectory $win32Peer `
    -Port $X64SubscriberPort `
    -Message 'win32-to-x64'

Write-Host 'PASS: cross-architecture interoperability succeeded in both directions'
