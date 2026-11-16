# M2 smoke test: drives Meridian through window messages (no focus stealing,
# no input injection) and checks results via the window title.
#   - typed host -> https load       - search fallback
#   - back / forward (WM_APPCOMMAND)  - invalid certificates stay blocked
# Usage: smoke-m2.ps1 [-Exe path\Meridian.exe]
param([string]$Exe = "$PSScriptRoot\..\build\bin\Meridian.exe")

Add-Type @'
using System; using System.Runtime.InteropServices; using System.Text;
public static class M {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
}
'@

$results = @()
function Check($name, $ok, $detail) { $script:results += [pscustomobject]@{ Test = $name; Result = $(if ($ok) { 'PASS' } else { 'FAIL' }); Detail = $detail } }

Get-Process Meridian -ErrorAction SilentlyContinue | Stop-Process; Start-Sleep 1
$sw = [Diagnostics.Stopwatch]::StartNew()
$profileDir = Join-Path $env:TEMP ('meridian-smoke2-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$proc = Start-Process $Exe -ArgumentList @("--profile=$profileDir", 'about:blank') -PassThru
while ($proc.MainWindowHandle -eq 0 -and $sw.Elapsed.TotalSeconds -lt 15) { Start-Sleep -Milliseconds 50; $proc.Refresh() }
Check 'window appears' ($proc.MainWindowHandle -ne 0) ("{0:N2}s to first window" -f $sw.Elapsed.TotalSeconds)
$main = $proc.MainWindowHandle
$edit = [M]::FindWindowEx($main, [IntPtr]::Zero, 'EDIT', $null)

function Title { $sb = New-Object Text.StringBuilder 512; [void][M]::GetWindowText($main, $sb, 512); $sb.ToString() }
function Go($text) { [void][M]::SendMessage($edit, 0x000C, [IntPtr]::Zero, $text); [void][M]::SendMessage($edit, 0x0100, [IntPtr]0x0D, [IntPtr]0) }
function WaitTitle($re, $secs = 20) { $t = [Diagnostics.Stopwatch]::StartNew(); while ($t.Elapsed.TotalSeconds -lt $secs) { if ((Title) -match $re) { return $t.Elapsed.TotalSeconds }; Start-Sleep -Milliseconds 100 }; return -1 }

Go 'webkit.org';            $t = WaitTitle '^WebKit';                  Check 'typed host loads over https' ($t -ge 0) ("{0:N1}s  '{1}'" -f $t, (Title))
Check 'title uses em dash (UTF-8 source)' ((Title) -match ' — Meridian$') (Title)
Go 'example.com';           $t = WaitTitle 'Example Domain';           Check 'second site' ($t -ge 0) ("{0:N1}s" -f $t)
Go 'meridian webkit test';  $t = WaitTitle 'DuckDuckGo';               Check 'search fallback' ($t -ge 0) ("{0:N1}s  '{1}'" -f $t, (Title))
[void][M]::SendMessage($main, 0x0319, $main, [IntPtr](1 -shl 16));   $t = WaitTitle 'Example Domain'; Check 'back' ($t -ge 0) ("{0:N1}s" -f $t)
[void][M]::SendMessage($main, 0x0319, $main, [IntPtr](2 -shl 16));   $t = WaitTitle 'DuckDuckGo';     Check 'forward' ($t -ge 0) ("{0:N1}s" -f $t)

# Invalid certificates must NOT load (title must not change to the bad host).
foreach ($bad in 'expired.badssl.com', 'wrong.host.badssl.com', 'self-signed.badssl.com', 'untrusted-root.badssl.com') {
  $before = Title
  Go $bad
  Start-Sleep 6
  Check "blocked: $bad" ((Title) -eq $before) ("title='{0}'" -f (Title))
}
Go 'badssl.com'; $t = WaitTitle 'badssl'; Check 'valid badssl.com still loads' ($t -ge 0) ("{0:N1}s" -f $t)

$mem = Get-Process Meridian, WebKitWebProcess, WebKitNetworkProcess, WebKitGPUProcess -ErrorAction SilentlyContinue |
  Measure-Object WorkingSet64 -Sum
Check 'memory (all processes)' $true ("{0:N0} MB working set" -f ($mem.Sum / 1MB))

Get-Process Meridian -ErrorAction SilentlyContinue | Stop-Process; Start-Sleep 1; Remove-Item -Recurse -Force $profileDir -ErrorAction SilentlyContinue
$results | Format-Table -AutoSize | Out-String -Width 200
if ($results.Result -contains 'FAIL') { exit 1 }
