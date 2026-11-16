# M3 smoke test (tabs): drives Meridian through its opt-in automation channel
# (--automation, WM_COPYDATA) in a throwaway profile, and checks state dumps.
param([string]$Exe = "$PSScriptRoot\..\build\bin\Meridian.exe", [string]$ShotDir = "$PSScriptRoot\..\docs\screenshots", [switch]$KeepProfile)

Add-Type @'
using System; using System.Runtime.InteropServices;
public static class MA {
  [StructLayout(LayoutKind.Sequential)] public struct CDS { public IntPtr dwData; public int cbData; public IntPtr lpData; }
  [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, ref CDS l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, IntPtr t); // t=IntPtr: PowerShell turns $null strings into ""
  public static bool Send(IntPtr h, string cmd) {
    var cds = new CDS(); cds.dwData = (IntPtr)0x4D52; cds.cbData = cmd.Length * 2; cds.lpData = Marshal.StringToHGlobalUni(cmd);
    try { return SendMessage(h, 0x004A, IntPtr.Zero, ref cds) != IntPtr.Zero; } finally { Marshal.FreeHGlobal(cds.lpData); }
  }
}
'@

$profile = Join-Path $env:TEMP ("meridian-smoke-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
$dump = Join-Path $profile 'state.txt'
$results = @()
function Check($name, $ok, $detail) { $script:results += [pscustomobject]@{ Test = $name; Result = $(if ($ok) { 'PASS' } else { 'FAIL' }); Detail = $detail } }
function Windows { @(Get-Process Meridian -ErrorAction SilentlyContinue | ForEach-Object { $_ }) }
function MainHwnd { $h = [MA]::FindWindowEx([IntPtr]::Zero, [IntPtr]::Zero, 'MeridianBrowserWindow', [IntPtr]::Zero); $h }
function Cmd($c, $hwnd = (MainHwnd)) { [void][MA]::Send($hwnd, $c) }
function State {
  # Commands run asynchronously (re-posted inside Meridian), so never read a
  # stale dump: delete it, ask for a new one, wait until it exists.
  Remove-Item $dump -ErrorAction SilentlyContinue
  Cmd "dump $dump"
  $t = [Diagnostics.Stopwatch]::StartNew()
  while (-not (Test-Path $dump) -and $t.Elapsed.TotalSeconds -lt 5) { Start-Sleep -Milliseconds 30 }
  Start-Sleep -Milliseconds 30
  $lines = if (Test-Path $dump) { Get-Content $dump -Encoding utf8 } else { @() }
  $s = [pscustomobject]@{ windows = @(); closed = 0 }
  foreach ($l in $lines) {
    if ($l -match '^window (\d+) active=(-?\d+) maximized=(\d)') { $s.windows += [pscustomobject]@{ active = [int]$Matches[2]; tabs = @() } }
    elseif ($l -match '^tab (\d+) pinned=(\d) view=(\d) favicon=(\d) thumb=(\d) loading=(\d) back=(\d) state=(\d+) url=(.*?) title=(.*)$') {
      $s.windows[-1].tabs += [pscustomobject]@{ pinned = $Matches[2] -eq '1'; view = $Matches[3] -eq '1'; favicon = $Matches[4] -eq '1'; thumb = $Matches[5] -eq '1'; loading = $Matches[6] -eq '1'; back = $Matches[7] -eq '1'; state = [int]$Matches[8]; url = $Matches[9]; title = $Matches[10] } }
    elseif ($l -match '^closed (\d+)') { $s.closed = [int]$Matches[1] }
  }
  $s
}
function WaitFor($cond, $secs = 15) { $t = [Diagnostics.Stopwatch]::StartNew(); while ($t.Elapsed.TotalSeconds -lt $secs) { $s = State; if (& $cond $s) { return $s }; Start-Sleep -Milliseconds 250 }; State }
function Launch($url) {
  $args = @('--automation', "--profile=$profile"); if ($url) { $args += $url }
  Start-Process $Exe -ArgumentList $args | Out-Null
  $t = [Diagnostics.Stopwatch]::StartNew(); while (-not (MainHwnd) -and $t.Elapsed.TotalSeconds -lt 15) { Start-Sleep -Milliseconds 50 }
}

Get-Process Meridian -ErrorAction SilentlyContinue | Stop-Process; Start-Sleep 1
New-Item -ItemType Directory -Force $profile | Out-Null
Launch 'https://webkit.org/'

$s = WaitFor { param($s) $s.windows.Count -eq 1 -and $s.windows[0].tabs.Count -eq 1 -and $s.windows[0].tabs[0].title -eq 'WebKit' }
Check 'first tab loads' ($s.windows[0].tabs[0].title -eq 'WebKit') $s.windows[0].tabs[0].title

Cmd 'open https://example.com/'
Cmd 'open https://badssl.com/'
$s = WaitFor { param($s) $s.windows[0].tabs.Count -eq 3 -and $s.windows[0].tabs[2].title -match 'badssl' -and $s.windows[0].tabs[1].title -match 'Example' }
Check 'three tabs open' ($s.windows[0].tabs.Count -eq 3) (($s.windows[0].tabs | % title) -join ' | ')
Check 'new tab becomes active' ($s.windows[0].active -eq 2) "active=$($s.windows[0].active)"

$s = WaitFor { param($s) $s.windows[0].tabs[0].favicon -and $s.windows[0].tabs[2].favicon }
Check 'favicons fetched (webkit.org, badssl.com)' ($s.windows[0].tabs[0].favicon -and $s.windows[0].tabs[2].favicon) (($s.windows[0].tabs | % { "$($_.url)=$($_.favicon)" }) -join ' ')

Cmd 'activate 0'
$s = WaitFor { param($s) $s.windows[0].active -eq 0 }
Check 'switch tab' ($s.windows[0].active -eq 0 -and (Get-Process Meridian).MainWindowTitle -like 'WebKit*') (Get-Process Meridian).MainWindowTitle
Check 'thumbnail captured on switch-away' ($s.windows[0].tabs[2].thumb) "tab2 thumb=$($s.windows[0].tabs[2].thumb)"

Cmd 'openbg https://example.org/'
$s = WaitFor { param($s) $s.windows[0].tabs.Count -eq 4 -and $s.windows[0].tabs[1].title -match 'Example' }
Check 'background tab opens next to current, stays inactive' ($s.windows[0].active -eq 0 -and $s.windows[0].tabs[1].url -match 'example.org' -and $s.windows[0].tabs[1].view) "active=$($s.windows[0].active) tab1=$($s.windows[0].tabs[1].url)"

Cmd 'pin 3'   # badssl.com -> pinned, moves to the front
$s = State
Check 'pin moves tab to front' ($s.windows[0].tabs[0].pinned -and $s.windows[0].tabs[0].url -match 'badssl') (($s.windows[0].tabs | % { "$(if($_.pinned){'*'})$($_.url)" }) -join ' ')
Check 'active tab follows reorder' ($s.windows[0].tabs[$s.windows[0].active].url -match 'webkit') "active=$($s.windows[0].active)"

Cmd 'move 1 3'
$s = State
Check 'reorder' ($s.windows[0].tabs[3].url -match 'webkit' -and $s.windows[0].tabs[$s.windows[0].active].url -match 'webkit') (($s.windows[0].tabs | % url) -join ' ')

Cmd 'dup 3'
$s = WaitFor { param($s) $s.windows[0].tabs.Count -eq 5 -and $s.windows[0].tabs[4].title -eq 'WebKit' }
Check 'duplicate tab' ($s.windows[0].tabs.Count -eq 5 -and $s.windows[0].tabs[4].url -match 'webkit') (($s.windows[0].tabs | % url) -join ' ')

Cmd 'close 4'
$s = State
Check 'close tab' ($s.windows[0].tabs.Count -eq 4 -and $s.closed -eq 1) "tabs=$($s.windows[0].tabs.Count) closed=$($s.closed)"
Cmd 'reopen'
$s = WaitFor { param($s) $s.windows[0].tabs.Count -eq 5 }
Check 'reopen closed tab (Ctrl+Shift+T path)' ($s.windows[0].tabs.Count -eq 5 -and $s.closed -eq 0) "tabs=$($s.windows[0].tabs.Count)"

Cmd 'overview 1'; Start-Sleep 1.5
if ($ShotDir) { & "$PSScriptRoot\capture-window.ps1" -ProcessName Meridian -Screen -Out (Join-Path $ShotDir 'm3-overview.png') 2>&1 | Out-Null }
Cmd 'overview 0'; Start-Sleep 1
if ($ShotDir) { & "$PSScriptRoot\capture-window.ps1" -ProcessName Meridian -Screen -Out (Join-Path $ShotDir 'm3-tabs.png') 2>&1 | Out-Null }

$mainWindow = MainHwnd   # the torn-off window will be on top afterwards; keep targeting this one
Cmd 'tearoff 2'
Start-Sleep 2
$s = State
Check 'tear off tab into new window' ($s.windows.Count -eq 2 -and $s.windows[0].tabs.Count -eq 4 -and $s.windows[1].tabs.Count -eq 1) "windows=$($s.windows.Count) tabs=$(($s.windows | % { $_.tabs.Count }) -join '+')"
Check 'moved tab keeps its page' ($s.windows[1].tabs[0].view -and $s.windows[1].tabs[0].title -ne '') "url=$($s.windows[1].tabs[0].url) title=$($s.windows[1].tabs[0].title)"

# Navigate in a tab so it has back history, then quit and relaunch.
Cmd 'activate 1' $mainWindow
Cmd 'nav https://example.com/' $mainWindow
$s = WaitFor { param($s) $s.windows[0].tabs[1].title -match 'Example Domain' }
Check 'navigate within a tab' ($s.windows[0].tabs[1].url -match 'example.com') $s.windows[0].tabs[1].url

# Close the torn-off window first, then the main one: the session keeps the last window.
$hwnds = @(); $h = [IntPtr]::Zero
while ($true) { $h = [MA]::FindWindowEx([IntPtr]::Zero, $h, 'MeridianBrowserWindow', [IntPtr]::Zero); if ($h -eq [IntPtr]::Zero) { break }; $hwnds += $h }
foreach ($w in ($hwnds | Where-Object { $_ -ne $mainWindow })) { Cmd 'closewindow' $w; Start-Sleep 1 }
Cmd 'closewindow' $mainWindow
$t = [Diagnostics.Stopwatch]::StartNew(); while ((Windows).Count -and $t.Elapsed.TotalSeconds -lt 10) { Start-Sleep -Milliseconds 200 }
Check 'app exits after last window closes' (-not (Windows).Count) "running=$((Windows).Count)"

$sw = [Diagnostics.Stopwatch]::StartNew()
Launch $null
$s = WaitFor { param($s) $s.windows.Count -ge 1 -and $s.windows[0].tabs.Count -ge 1 }
$restored = ($s.windows | % { ($_.tabs | % { "$(if($_.pinned){'*'})$($_.url)" }) -join ',' }) -join ' / '
Check 'session restored (last window)' ($s.windows.Count -eq 1 -and $s.windows[0].tabs.Count -ge 4) "restored: $restored"
Check 'pinned state restored' ($s.windows[0].tabs[0].pinned) ''
$lazy = @($s.windows[0].tabs | Where-Object { $_.view }).Count
Check 'restored background tabs are lazy (1 web view)' ($lazy -eq 1) "views=$lazy of $($s.windows[0].tabs.Count)"
$idx = 0; for ($i = 0; $i -lt $s.windows[0].tabs.Count; $i++) { if ($s.windows[0].tabs[$i].url -match 'example.com') { $idx = $i } }
Cmd "activate $idx"
$s = WaitFor { param($s) $s.windows[0].tabs[$idx].title -match 'Example Domain' }
Check 'restored tab loads on first show' ($s.windows[0].tabs[$idx].title -match 'Example') $s.windows[0].tabs[$idx].title
$s = WaitFor { param($s) $s.windows[0].tabs[$idx].back }
Check 'restored tab has back history' ($s.windows[0].tabs[$idx].back) "back=$($s.windows[0].tabs[$idx].back) state=$($s.windows[0].tabs[$idx].state) bytes"
[void][MA]::Send((MainHwnd), 'activate ' + $idx)
$hwnd = MainHwnd
Add-Type -Name AC -Namespace W -MemberDefinition '[DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);'
[void][W.AC]::SendMessage($hwnd, 0x0319, $hwnd, [IntPtr](1 -shl 16))   # APPCOMMAND_BROWSER_BACKWARD
$s = WaitFor { param($s) $s.windows[0].tabs[$idx].url -notmatch 'example.com' }
Check 'back history survives restart' ($s.windows[0].tabs[$idx].url -notmatch 'example.com') "after back: $($s.windows[0].tabs[$idx].url)"

Get-Process Meridian -ErrorAction SilentlyContinue | Stop-Process
Start-Sleep 1
if ($KeepProfile) { "profile kept: $profile" } else { Remove-Item -Recurse -Force $profile -ErrorAction SilentlyContinue }
$results | Format-Table -AutoSize -Wrap | Out-String -Width 220
if ($results.Result -contains 'FAIL') { exit 1 }
