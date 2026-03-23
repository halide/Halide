param([string]$src, [string]$dstDir)

try {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($dstDir)
    $hash = [System.Security.Cryptography.SHA512]::Create().ComputeHash($bytes)
    $key = "Halide-" + ([Convert]::ToBase64String($hash) -replace ('/', '-'))

    $m = New-Object System.Threading.Mutex($false, $key)
    if (!$m) {
        throw "Failed to create mutex $key"
    }

    $m.WaitOne() | Out-Null

    $name = Split-Path $src -leaf
    $dst = Join-Path $dstDir $name
    if (Test-Path $dst) {
        $srcTime = (Get-Item $src).LastWriteTime
        $dstTime = (Get-Item $dst).LastWriteTime
        if ($dstTime -ge $srcTime) {
            Return
        }
    }

    Copy-Item $src $dstDir
} finally {
    if ($m) {
        $m.ReleaseMutex() | Out-Null
        $m.Dispose() | Out-Null
    }
}
