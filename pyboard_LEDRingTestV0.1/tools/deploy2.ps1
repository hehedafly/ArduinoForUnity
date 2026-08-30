# deploy2.ps1 - write a local file to pyboard via chunked-hex REPL (reliable, dev tool)
# usage: pwsh -File deploy2.ps1 -File <local> -Remote /flash/<name>
param(
    [string]$Port = "COM7",
    [string]$File,
    [string]$Remote,
    [int]$ChunkHex = 180
)
$ErrorActionPreference = 'Stop'

function ReadBytes($ms) {
    $deadline = (Get-Date).AddMilliseconds($ms)
    $raw = New-Object System.Collections.Generic.List[byte]
    $buf = New-Object byte[] 4096
    while ((Get-Date) -lt $deadline) {
        $avail = $sp.BytesToRead
        if ($avail -gt 0) {
            $n = $sp.Read($buf, 0, [Math]::Min($buf.Length, $avail))
            for ($i = 0; $i -lt $n; $i++) { $raw.Add($buf[$i]) }
        }
        Start-Sleep -Milliseconds 10
    }
    return $raw.ToArray()
}

function Send-Line([string]$line) {
    $b = [System.Text.Encoding]::ASCII.GetBytes($line + "`r")
    $sp.Write($b, 0, $b.Length)
    Start-Sleep -Milliseconds 120
    $null = ReadBytes 60
}

$content = [System.IO.File]::ReadAllText($File)
$content = $content -replace "`r`n", "`n"
$content = [System.Text.Encoding]::ASCII.GetString([System.Text.Encoding]::ASCII.GetBytes($content))
$rawBytes = [System.Text.Encoding]::ASCII.GetBytes($content)
$hex = -join ($rawBytes | ForEach-Object { $_.ToString('x2') })

$sp = New-Object System.IO.Ports.SerialPort($Port, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One))
$sp.ReadTimeout = 1500
$sp.WriteTimeout = 2000
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.Open()

try {
    $sp.Write([byte[]](0x03), 0, 1)
    Start-Sleep -Milliseconds 250
    $null = ReadBytes 300

    Send-Line "_s = ''"
    for ($i = 0; $i -lt $hex.Length; $i += $ChunkHex) {
        $chunk = $hex.Substring($i, [Math]::Min($ChunkHex, $hex.Length - $i))
        Send-Line ("_s += '" + $chunk + "'")
    }
    Send-Line ("f=open('$Remote','wb'); f.write(ubinascii.unhexlify(_s)); f.close()")
    Send-Line ("print('WROTE ' + repr('$Remote') + ' len=' + str(len(open('$Remote').read())))")
    Start-Sleep -Milliseconds 400
    $out = ReadBytes 800
    if ($null -eq $out -or $out.Length -eq 0) { Write-Output "(no response)" }
    else {
        $text = [System.Text.Encoding]::UTF8.GetString($out)
        if ($text -match 'WROTE[^\r\n]*') { Write-Output $matches[0] }
        elseif ($text -match 'Traceback[^\r\n]*') { Write-Output ("ERR: " + $text) }
        else { Write-Output ("(no marker, tail=)" + $text.Substring([Math]::Max(0, $text.Length - 300))) }
    }
}
finally {
    if ($sp.IsOpen) { $sp.Close() }
}
