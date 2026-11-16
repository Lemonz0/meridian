# Captures a top-level window of a process to PNG.
#   default : PrintWindow(PW_RENDERFULLCONTENT). Misses GPU-composited WebKit
#             child windows (they come out black).
#   -Screen : screen pixels. The window is made topmost for the capture, and
#             the capture is ABORTED (nothing saved) unless every sample point
#             inside the window rect belongs to the target window. This stops
#             other apps' content from leaking into the screenshot.
# Usage: capture-window.ps1 -ProcessName Meridian -Out shot.png [-Screen]
param([Parameter(Mandatory)][string]$ProcessName, [Parameter(Mandatory)][string]$Out, [switch]$Screen)

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Win {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int attr, out RECT r, int size);
}
'@

$p = Get-Process -Name $ProcessName -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { throw "No window for $ProcessName" }
$h = $p.MainWindowHandle

# Visible bounds (excludes the invisible resize border).
[Win+RECT]$r = New-Object Win+RECT
if ([Win]::DwmGetWindowAttribute($h, 9, [ref]$r, 16) -ne 0) { [void][Win]::GetWindowRect($h, [ref]$r) }
$w = $r.R - $r.L; $hgt = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $hgt
$g = [System.Drawing.Graphics]::FromImage($bmp)

if ($Screen) {
  $TOPMOST = [IntPtr](-1); $NOTOPMOST = [IntPtr](-2); $flags = 0x0001 -bor 0x0002 -bor 0x0010 # NOSIZE|NOMOVE|NOACTIVATE
  [void][Win]::SetWindowPos($h, $TOPMOST, 0, 0, 0, 0, $flags)
  try {
    Start-Sleep -Milliseconds 500
    foreach ($fx in 0.05, 0.5, 0.95) { foreach ($fy in 0.05, 0.5, 0.95) {
      $pt = New-Object Win+POINT; $pt.X = [int]($r.L + $w * $fx); $pt.Y = [int]($r.T + $hgt * $fy)
      $root = [Win]::GetAncestor([Win]::WindowFromPoint($pt), 2) # GA_ROOT
      if ($root -ne $h) { $g.Dispose(); $bmp.Dispose(); throw "Aborted: window not on top at ($($pt.X),$($pt.Y)); nothing saved." }
    } }
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
  } finally {
    [void][Win]::SetWindowPos($h, $NOTOPMOST, 0, 0, 0, 0, $flags)
  }
} else {
  $hdc = $g.GetHdc()
  [void][Win]::PrintWindow($h, $hdc, 2)
  $g.ReleaseHdc($hdc)
}
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
"title: " + $p.MainWindowTitle
"saved: $Out (${w}x${hgt})"
