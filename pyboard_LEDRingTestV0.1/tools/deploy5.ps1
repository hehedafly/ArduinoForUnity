# deploy5.ps1 - deploy a file via chunked-hex with FIXED delays (no prompt-wait regex), split into parts
param(
    [string]$Port = "COM7",
    [string]$File,
    [string]$Remote,
    [int]$ChunkHex = 200,
    [int]$MaxBytes = 3000,
    [int]$DelayMs = 400
)
$ErrorActionPreference = 'Stop'

$content = [System.IO.File]::ReadAllText($File)
$content = $content -replace "`r`n", "`n"
$content = [System.Text.Encoding]::ASCII.GetString([System.Text.Encoding]::ASCII.GetBytes($content))
$allBytes = [System.Text.Encoding]::ASCII.GetBytes($content)

$sp = New-Object System.IO.Ports.SerialPort($Port, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One))
$sp.ReadTimeout = 1500
$sp.WriteTimeout = 2000
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.Open()

function Drain {
    $b = New-Object byte[] 4096
    while ($sp.BytesToRead -gt 0) { $n = $sp.Read($b, 0, [Math]::Min($b.Length, $sp.BytesToRead)) | Out-Null; $null = $n }
}

function SendLine([string]$line) {
    $b = [System.Text.Encoding]::ASCII.GetBytes($line + "`r")
    $sp.Write($b, 0, $b.Length)
    Start-Sleep -Milliseconds $DelayMs
    Drain
}

try {
    $sp.Write([byte[]](0x03), 0, 1); Start-Sleep -Milliseconds 400; Drain
    $sp.Write([byte[]](0x0D), 0, 1); Start-Sleep -Milliseconds 300; Drain

    $off = 0; $part = 0
    while ($off -lt $allBytes.Length) {
        $len = [Math]::Min($MaxBytes, $allBytes.Length - $off)
        $hex = ''
        for ($i = $off; $i -lt ($off + $len); $i++) { $hex += $allBytes[$i].ToString('x2') }
        $mode = if ($part -eq 0) { 'wb' } else { 'ab' }

        SendLine "_s = ''"
        for ($i = 0; $i -lt $hex.Length; $i += $ChunkHex) {
            $chunk = $hex.Substring($i, [Math]::Min($ChunkHex, $hex.Length - $i))
            SendLine ("_s += '" + $chunk + "'")
        }
        SendLine ("f=open('$Remote','$mode'); f.write(ubinascii.unhexlify(_s)); f.close()")
        $off += $len; $part++
        Write-Output ("part $part done ($len bytes, $mode)")
    }

    SendLine "print('FINALSIZE ' + str(os.stat('$Remote')[6]))"
    Start-Sleep -Milliseconds 500
    Drain
}
finally {
    if ($sp.IsOpen) { $sp.Close() }
}
