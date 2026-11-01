# Capture the DOSBox-X window's client area once the guest probe signals it.
#
# Waits for ReadyFile (written by DISPBASE.EXE when given a hold time), then
# renders the emulator window with PrintWindow(PW_CLIENTONLY |
# PW_RENDERFULLCONTENT) and saves it as PNG. This is what DOSBox-X displays,
# independent of the guest display driver's own readback.
param(
    [Parameter(Mandatory = $true)][string]$ReadyFile,
    [Parameter(Mandatory = $true)][string]$OutFile,
    [string]$TitleLike = '*S3 display baseline*',
    [int]$TimeoutSec = 150,
    # If set, capture anyway after this many seconds without ReadyFile
    # (for runs where Windows is expected to hang before the probe starts).
    [int]$FallbackSec = 0
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class S3Capture {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
# Measure in physical pixels; with display scaling a DPI-unaware process
# would see a resampled client area.
[void][S3Capture]::SetProcessDPIAware()

$start = Get-Date
$deadline = $start.AddSeconds($TimeoutSec)
$trigger = 'READY'
while (-not (Test-Path -LiteralPath $ReadyFile)) {
    if ($FallbackSec -gt 0 -and (Get-Date) -gt $start.AddSeconds($FallbackSec)) {
        $trigger = 'FALLBACK'
        break
    }
    if ((Get-Date) -gt $deadline) { Write-Output 'CAPTURE=TIMEOUT_WAITING_FOR_READY'; exit 1 }
    Start-Sleep -Milliseconds 250
}
# Let the emulator present at least one more frame after the readback.
Start-Sleep -Seconds 2

$window = Get-Process | Where-Object { $_.MainWindowTitle -like $TitleLike } | Select-Object -First 1
if ($null -eq $window) { Write-Output 'CAPTURE=NO_WINDOW'; exit 1 }

$rect = New-Object S3Capture+RECT
[void][S3Capture]::GetClientRect($window.MainWindowHandle, [ref]$rect)
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -le 0 -or $height -le 0) { Write-Output 'CAPTURE=EMPTY_CLIENT'; exit 1 }

$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$hdc = $graphics.GetHdc()
$ok = [S3Capture]::PrintWindow($window.MainWindowHandle, $hdc, 3)
$graphics.ReleaseHdc($hdc)
$graphics.Dispose()
if (-not $ok) { $bitmap.Dispose(); Write-Output 'CAPTURE=PRINTWINDOW_FAILED'; exit 1 }
$bitmap.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
Write-Output "CAPTURE=OK trigger=$trigger ${width}x${height} title=$($window.MainWindowTitle)"
