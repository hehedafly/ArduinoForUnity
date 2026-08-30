# repl.ps1 — 向 pyboard REPL 发送单行命令并捕获输出（开发期工具，非部署）
# 用法:
#   pwsh -File repl.ps1 -Port COM7 -Cmd "import os; os.listdir('/')" -WaitMs 1500
#   pwsh -File repl.ps1 -Cmd "print(123)"
param(
    [string]$Port = "COM7",
    [string]$Cmd,
    [int]$WaitMs = 1500,
    [switch]$NoReset
)

$ErrorActionPreference = 'Stop'

function Read-Available($port, $ms) {
    $deadline = (Get-Date).AddMilliseconds($ms)
    $raw = New-Object System.Collections.Generic.List[byte]
    $buf = New-Object byte[] 4096
    while ((Get-Date) -lt $deadline) {
        $avail = $port.BytesToRead
        if ($avail -gt 0) {
            $n = $port.Read($buf, 0, [Math]::Min($buf.Length, $avail))
            for ($i = 0; $i -lt $n; $i++) { $raw.Add($buf[$i]) }
        }
        Start-Sleep -Milliseconds 20
    }
    return $raw.ToArray()
}

$sp = New-Object System.IO.Ports.SerialPort($Port, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One))
$sp.ReadTimeout = 2000
$sp.WriteTimeout = 2000
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.Open()

try {
    if (-not $NoReset) {
        # Ctrl-C 打断当前程序/清空输入，回到干净提示符
        $sp.Write([byte[]](0x03), 0, 1)
        Start-Sleep -Milliseconds 300
        $null = Read-Available $sp 400
    }

    if ($Cmd) {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Cmd)
        $sp.Write($bytes, 0, $bytes.Length)
        $sp.Write([byte[]](0x0D), 0, 1)   # 回车执行
    }

    $out = Read-Available $sp $WaitMs
    $text = [System.Text.Encoding]::UTF8.GetString($out)
    # 去掉回显的命令行本身，输出更干净
    if ($Cmd) {
        $echoed = $Cmd + "`r`n"
        if ($text.StartsWith($Cmd)) {
            $text = $text.Substring($Cmd.Length).TrimStart("`r", "`n")
        }
    }
    Write-Output $text
}
finally {
    if ($sp.IsOpen) { $sp.Close() }
}
