# Reports what WM_NCHITTEST returns at the centre of each DWM caption button,
# windowed and maximized. Expected: 8 (HTMINBUTTON), 9 (HTMAXBUTTON), 20 (HTCLOSE).
param([string]$Exe = "$PSScriptRoot\..\build\bin\Meridian.exe")
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class P {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT r, int s);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
}
'@
Get-Process Meridian -ErrorAction SilentlyContinue | Stop-Process; Start-Sleep 1
$p = Start-Process $Exe -ArgumentList 'about:blank' -PassThru
while ($p.MainWindowHandle -eq 0) { Start-Sleep -Milliseconds 50; $p.Refresh() }
$h = $p.MainWindowHandle
$names = @{ 2 = 'CAPTION'; 1 = 'CLIENT'; 8 = 'MINBUTTON'; 9 = 'MAXBUTTON'; 20 = 'CLOSE'; 12 = 'TOP'; 0 = 'NOWHERE' }
$ok = $true
foreach ($state in 'windowed', 'maximized') {
  if ($state -eq 'maximized') { [void][P]::ShowWindow($h, 3); Start-Sleep -Milliseconds 700 }
  $win = New-Object P+RECT; [void][P]::GetWindowRect($h, [ref]$win)
  $btn = New-Object P+RECT; [void][P]::DwmGetWindowAttribute($h, 5, [ref]$btn, 16)  # DWMWA_CAPTION_BUTTON_BOUNDS (window coords)
  $w = $btn.R - $btn.L; $y = $win.T + [int](($btn.T + $btn.B) / 2)
  "{0}: window=({1},{2})-({3},{4}) buttons(window-rel)=({5},{6})-({7},{8})" -f $state, $win.L, $win.T, $win.R, $win.B, $btn.L, $btn.T, $btn.R, $btn.B
  $expect = 8, 9, 20
  for ($i = 0; $i -lt 3; $i++) {
    $x = $win.L + $btn.L + [int]($w * (2 * $i + 1) / 6)
    $lp = [IntPtr](($y -band 0xFFFF) -shl 16 -bor ($x -band 0xFFFF))
    $r = [int][P]::SendMessage($h, 0x0084, [IntPtr]::Zero, $lp)
    $pass = $r -eq $expect[$i]; if (-not $pass) { $ok = $false }
    "   button {0} at ({1},{2}) -> {3} {4}" -f ($i + 1), $x, $y, $names[$r], $(if ($pass) { 'OK' } else { "EXPECTED $($names[$expect[$i]])" })
  }
  if ($state -eq 'maximized') {
    # Screen's top-right pixel must hit Close (mouse slammed into the corner).
    Add-Type -AssemblyName System.Windows.Forms
    $scr = [System.Windows.Forms.Screen]::FromHandle($h).Bounds
    $x = $scr.Right - 1; $y = $scr.Top
    $r = [int][P]::SendMessage($h, 0x0084, [IntPtr]::Zero, [IntPtr](($y -band 0xFFFF) -shl 16 -bor ($x -band 0xFFFF)))
    if ($r -ne 20) { $ok = $false }
    "   screen corner ({0},{1}) -> {2} {3}" -f $x, $y, $names[$r], $(if ($r -eq 20) { 'OK' } else { 'EXPECTED CLOSE' })
  }
}
Stop-Process -Id $p.Id
if (-not $ok) { exit 1 }
