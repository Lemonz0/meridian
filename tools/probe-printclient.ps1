# Experiment: does WebKit's WKView render real pixels on WM_PRINTCLIENT?
param([string]$Out = "$env:TEMP\printclient.png")
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices; using System.Text;
public static class PC {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
'@
$main = (Get-Process Meridian).MainWindowHandle
$found = [IntPtr]::Zero
[void][PC]::EnumChildWindows($main, { param($h, $l)
  $sb = New-Object Text.StringBuilder 128; [void][PC]::GetClassName($h, $sb, 128)
  if ($sb.ToString() -ne 'EDIT' -and [PC]::IsWindowVisible($h)) { "child class: $sb"; $script:found = $h }
  return $true }, [IntPtr]::Zero)
$r = New-Object PC+RECT; [void][PC]::GetClientRect($found, [ref]$r)
$bmp = New-Object Drawing.Bitmap $r.R, $r.B
$g = [Drawing.Graphics]::FromImage($bmp); $g.Clear([Drawing.Color]::Magenta); $hdc = $g.GetHdc()
$sw = [Diagnostics.Stopwatch]::StartNew()
[void][PC]::SendMessage($found, 0x0318, $hdc, [IntPtr]0x14)   # WM_PRINTCLIENT, PRF_CLIENT|PRF_ERASEBKGND
$ms = $sw.Elapsed.TotalMilliseconds
$g.ReleaseHdc($hdc); $g.Dispose()
# Count non-magenta, non-black pixels on a grid to judge whether content was drawn.
$drawn = 0; $total = 0
for ($y = 5; $y -lt $bmp.Height; $y += 20) { for ($x = 5; $x -lt $bmp.Width; $x += 20) {
  $c = $bmp.GetPixel($x, $y); $total++
  if (-not (($c.R -eq 255 -and $c.G -eq 0 -and $c.B -eq 255) -or ($c.R + $c.G + $c.B -lt 10))) { $drawn++ } } }
$bmp.Save($Out); $bmp.Dispose()
"size {0}x{1}, WM_PRINTCLIENT took {2:N1} ms, drawn pixels {3}/{4} -> {5}" -f $r.R, $r.B, $ms, $drawn, $total, $Out
