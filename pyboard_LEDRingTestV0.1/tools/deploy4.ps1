# deploy4.ps1 - deploy a file in <=MaxBytes parts (append after first) to avoid REPL size limits
param(
    [string]$Port = "COM7",
    [string]$File,
    [string]$Remote,
    [int]$ChunkHex = 220,
    [int]$MaxBytes = 3400
)
$ErrorActionPreference = 'Stop'

function ReadAvailable {
    $raw = New-Object System.Collections.Generic.List[byte]
    $buf = New-Object byte[] 2048
    $avail = $sp.BytesToRead
    while ($avail -gt 0) {
        $n = $sp.Read($buf, 0, [Math]::Min($buf.Length, $avail))
        for ($i = 0; $i -lt $n; $i++) { $raw.Add($buf[$i]) }
        $avail = $sp.BytesToRead
    }
    return ,$raw.ToArray()
}

function Send-Line([string]$line) {
    $b = [System.Text.Encoding]::ASCII.GetBytes($line + "`r")
    $sp.Write($b, 0, $b.Length)
    $deadline = (Get-Date).AddSeconds(3)
    $acc = New-Object System.Collections.Generic.List[byte]
    $buf = New-Object byte[] 1024
    while ((Get-Date) -lt $deadline) {
        $avail = $sp.BytesToRead
        if ($avail -gt 0) {
            $n = $sp.Read($buf, 0, [Math]::Min($buf.Length, $avail))
            for ($i = 0; $i -lt $n; $i++) { $acc.Add($buf[$i]) }
        }
        $s = [System.Text.Encoding]::ASCII.GetString($acc.ToArray())
        if ($s -match '>>>\s*$') { break }
        Start-Sleep -Milliseconds 10
    }
}

function Deploy-Bytes([byte[]]$data, [string]$mode) {
    $hex = -join ($data | ForEach-Object { $_.ToString('x2') })
    Send-Line "_s = ''"
    for ($i = 0; $i -lt $hex.Length; $i += $ChunkHex) {
        $chunk = $hex.Substring($i, [Math]::Min($ChunkHex, $hex.Length - $i))
        Send-Line ("_s += '" + $chunk + "'")
    }
    Send-Line ("f=open('$Remote','$mode'); f.write(ubinascii.unhexlify(_s)); f.close()")
}

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

try {
    $sp.Write([byte[]](0x03), 0, 1)
    Start-Sleep -Milliseconds 300
    $null = ReadAvailable
    $sp.Write([byte[]](0x0D), 0, 1)
    Start-Sleep -Milliseconds 200
    $null = ReadAvailable

    $off = 0
    $part = 0
    while ($off -lt $allBytes.Length) {
        $len = [Math]::Min($MaxBytes, $allBytes.Length - $off)
        $slice = New-Object byte[] $len
        [Array]::Copy($allBytes, $off, $slice, 0, $len)
        $mode = if ($part -eq 0) { 'wb' } else { 'ab' }
        Deploy-Bytes $slice $mode
        $off += $len
        $part++
        Write-Output ("part {0} done ({1} bytes, mode {2})" -f $part, $len, $mode)
    }

    Send-Line ("print('FINAL ' + repr('$Remote') + ' ' + str(os.stat('$Remote')[6]))")
    Start-Sleep -Milliseconds 300
    $out = ReadAvailable
    if ($null -ne $out) {
        $text = [System.Text.Encoding]::ASCII.GetString($out)
        if ($text -match 'FINAL\s+[^\r\n]*') { foreach ($m in [regex]::Matches($text, 'FINAL\s+[^\r\n]*')) { Write-Output $m.Value } }
    }
}
finally {
    if ($sp.IsOpen) { $sp.Close() }
}
