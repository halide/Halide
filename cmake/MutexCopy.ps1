param([string]$src, [string]$dstDir)

$ErrorActionPreference = 'Stop'

function Test-UpToDate([string]$src, [string]$dst) {
    if (!(Test-Path $dst)) {
        return $false
    }
    return (Get-Item $dst).LastWriteTime -ge (Get-Item $src).LastWriteTime
}

$name = Split-Path $src -leaf
$dst = Join-Path $dstDir $name

if (Test-UpToDate $src $dst) {
    return
}

$bytes = [System.Text.Encoding]::UTF8.GetBytes($dstDir)
$hash = [System.Security.Cryptography.SHA512]::Create().ComputeHash($bytes)
$key = "Halide-" + ([Convert]::ToBase64String($hash) -replace ('/', '-'))

$m = New-Object System.Threading.Mutex($false, $key)
$acquired = $false
try {
    try {
        $acquired = $m.WaitOne(120000)
    } catch [System.Threading.AbandonedMutexException] {
        # A previous copy was killed/cancelled while holding the lock. We still
        # got ownership; the destination may be left mid-copy from that run, but
        # the recheck below re-copies if it's not known-good, so it's safe to
        # just proceed rather than failing the build over it.
        $acquired = $true
    }
    if (!$acquired) {
        throw "Timed out waiting for lock on $dstDir"
    }

    if (Test-UpToDate $src $dst) {
        return
    }

    Copy-Item $src $dstDir
} finally {
    if ($acquired) {
        $m.ReleaseMutex() | Out-Null
    }
    $m.Dispose() | Out-Null
}
