# M5 smoke test (Reader, Reading List, sidebar, Tab Groups): drives Meridian
# through its opt-in automation channel (--automation, WM_COPYDATA) in a
# throwaway profile, like smoke-m3/m4. Never steals focus. Visits
# en.wikipedia.org (an article, for Reader), example.com and example.org.
param([string]$Exe = "$PSScriptRoot\..\build\bin\Meridian.exe", [string]$ShotDir = "$PSScriptRoot\..\docs\screenshots",
      [string]$Article = 'https://en.wikipedia.org/wiki/WebKit', [switch]$KeepProfile)

Add-Type @'
using System; using System.Runtime.InteropServices;
public static class MA {
  [StructLayout(LayoutKind.Sequential)] public struct CDS { public IntPtr dwData; public int cbData; public IntPtr lpData; }
  [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, ref CDS l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, IntPtr t);
  public static bool Send(IntPtr h, string cmd) {
    var cds = new CDS(); cds.dwData = (IntPtr)0x4D52; cds.cbData = cmd.Length * 2; cds.lpData = Marshal.StringToHGlobalUni(cmd);
    try { return SendMessage(h, 0x004A, IntPtr.Zero, ref cds) != IntPtr.Zero; } finally { Marshal.FreeHGlobal(cds.lpData); }
  }
}
'@

$profile = Join-Path $env:TEMP ("meridian-smoke-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
$dump = Join-Path $profile 'state.txt'
$uiDump = Join-Path $profile 'ui.txt'
$results = @()
function Check($name, $ok, $detail) { $script:results += [pscustomobject]@{ Test = $name; Result = $(if ($ok) { 'PASS' } else { 'FAIL' }); Detail = $detail } }
function Windows { @(Get-Process Meridian -ErrorAction SilentlyContinue | ForEach-Object { $_ }) }
function MainHwnd { [MA]::FindWindowEx([IntPtr]::Zero, [IntPtr]::Zero, 'MeridianBrowserWindow', [IntPtr]::Zero) }
function Cmd($c) { [void][MA]::Send((MainHwnd), $c) }
function ReadDump($cmd, $path) {
  Remove-Item $path -ErrorAction SilentlyContinue
  Cmd "$cmd $path"
  $t = [Diagnostics.Stopwatch]::StartNew()
  while (-not (Test-Path $path) -and $t.Elapsed.TotalSeconds -lt 5) { Start-Sleep -Milliseconds 30 }
  if (Test-Path $path) { @(Get-Content $path -Encoding utf8) } else { @() }
}
function State {
  $s = [pscustomobject]@{ active = -1; group = 0; tabs = @() }
  foreach ($l in (ReadDump 'dump' $dump)) {
    if ($l -match '^window 0 active=(-?\d+) maximized=\d group=(\d+)') { $s.active = [int]$Matches[1]; $s.group = [long]$Matches[2] }
    elseif ($l -match '^tab (\d+) pinned=(\d) .* loading=(\d) .* url=(.*?) title=(.*)$') { $s.tabs += [pscustomobject]@{ pinned = $Matches[2] -eq '1'; loading = $Matches[3] -eq '1'; url = $Matches[4]; title = $Matches[5] } }
  }
  $s
}
function Ui {
  $u = [pscustomobject]@{ startpage = $false; startReading = @(); readerAvailable = $false; readerActive = $false; group = 0; label = ''; stash = 0;
                          ungrouped = 0; groups = @(); reading = @(); sidebar = $false; pane = ''; rows = @() }
  foreach ($l in (ReadDump 'dumpui' $uiDump)) {
    if ($l -match '^startpage visible=(\d)') { $u.startpage = $Matches[1] -eq '1' }
    elseif ($l -match '^startreading \d+ url=(.*?) title=') { $u.startReading += $Matches[1] }
    elseif ($l -match '^reader available=(\d) active=(\d)') { $u.readerAvailable = $Matches[1] -eq '1'; $u.readerActive = $Matches[2] -eq '1' }
    elseif ($l -match '^group current=(\d+) label=(.*) stash=(\d+) ungrouped=(\d+)') { $u.group = [long]$Matches[1]; $u.label = $Matches[2]; $u.stash = [int]$Matches[3]; $u.ungrouped = [int]$Matches[4] }
    elseif ($l -match '^tabgroup \d+ id=(\d+) tabs=(\d+) name=(.*)$') { $u.groups += [pscustomobject]@{ id = [long]$Matches[1]; tabs = [int]$Matches[2]; name = $Matches[3] } }
    elseif ($l -match '^reading \d+ id=(\d+) unread=(\d) url=(.*?) title=(.*?) excerpt=(.*)$') { $u.reading += [pscustomobject]@{ id = [long]$Matches[1]; unread = $Matches[2] -eq '1'; url = $Matches[3]; title = $Matches[4]; excerpt = $Matches[5] } }
    elseif ($l -match '^sidebar open=(\d) pane=(\w)') { $u.sidebar = $Matches[1] -eq '1'; $u.pane = $Matches[2] }
    elseif ($l -match '^row \d+ kind=(\w+) id=(\d+) depth=(\d+) current=(\d) title=(.*)$') { $u.rows += [pscustomobject]@{ kind = $Matches[1]; id = [long]$Matches[2]; current = $Matches[4] -eq '1'; title = $Matches[5] } }
  }
  $u
}
function WaitFor($get, $cond, $secs = 20) { $t = [Diagnostics.Stopwatch]::StartNew(); while ($t.Elapsed.TotalSeconds -lt $secs) { $s = & $get; if (& $cond $s) { return $s }; Start-Sleep -Milliseconds 250 }; & $get }
function Launch($url) {
  $a = @('--automation', "--profile=$profile"); if ($url) { $a += $url }
  Start-Process $Exe -ArgumentList $a | Out-Null
  $t = [Diagnostics.Stopwatch]::StartNew(); while (-not (MainHwnd) -and $t.Elapsed.TotalSeconds -lt 15) { Start-Sleep -Milliseconds 50 }
}
function Shot($name) { if ($ShotDir) { & "$PSScriptRoot\capture-window.ps1" -ProcessName Meridian -Screen -Out (Join-Path $ShotDir $name) 2>&1 | Out-Null } }
function Loaded($pattern) { { param($s) $s.active -ge 0 -and $s.tabs[$s.active].url -match $pattern -and -not $s.tabs[$s.active].loading }.GetNewClosure() }
function Urls($s) { ($s.tabs | % { "$(if ($_.pinned) { '*' })$($_.url)" }) -join ' ' }

Get-Process Meridian -ErrorAction SilentlyContinue | Stop-Process; Start-Sleep 1
New-Item -ItemType Directory -Force $profile | Out-Null

# 1. Reader on an article; not on a plain page.
Launch $Article
$s = WaitFor { State } (Loaded 'wikipedia')
$u = WaitFor { Ui } { param($u) $u.readerAvailable }
Check 'Reader offered on an article' ($u.readerAvailable) "available=$($u.readerAvailable)"
Cmd 'reader 1'
$u = WaitFor { Ui } { param($u) $u.readerActive } 5
Check 'Reader shows' ($u.readerActive) "active=$($u.readerActive)"
Shot 'm5-reader.png'
Cmd 'readerstyle sepia 130 1'; Start-Sleep 1
Shot 'm5-reader-sepia.png'
Cmd 'readerstyle auto 100 0'
Cmd 'reader 0'
$u = WaitFor { Ui } { param($u) -not $u.readerActive } 5
Check 'Reader hides' (-not $u.readerActive -and $u.readerAvailable) "active=$($u.readerActive) available=$($u.readerAvailable)"

# 2. Reading List.
Cmd 'readinglist'
$u = WaitFor { Ui } { param($u) $u.reading.Count -eq 1 }
Check 'Ctrl+Shift+D adds to the Reading List' ($u.reading.Count -eq 1 -and $u.reading[0].unread) (($u.reading | % url) -join ' ')
Check 'Reading List item has an excerpt' ($u.reading[0].excerpt.Length -gt 20) $u.reading[0].excerpt
Cmd 'nav https://example.com/'
$s = WaitFor { State } (Loaded 'example.com')
Start-Sleep 2; $u = Ui
Check 'Reader not offered on a short page' (-not $u.readerAvailable) "available=$($u.readerAvailable)"
Cmd 'readinglist'
$u = WaitFor { Ui } { param($u) $u.reading.Count -eq 2 }
Check 'second item, newest first' ($u.reading.Count -eq 2 -and $u.reading[0].url -match 'example.com') (($u.reading | % url) -join ' ')
Cmd 'open about:blank'
$u = WaitFor { Ui } { param($u) $u.startpage -and $u.startReading.Count -eq 2 }
Check 'unread items on the start page' ($u.startReading.Count -eq 2) ($u.startReading -join ' ')
$wiki = ($u.reading | ? url -match 'wikipedia')[0]
Cmd "readingopen $($wiki.id)"
$s = WaitFor { State } (Loaded 'wikipedia')
$u = Ui
Check 'opening an item navigates and marks it read' ($s.tabs[$s.active].url -match 'wikipedia' -and -not ($u.reading | ? id -eq $wiki.id).unread) $s.tabs[$s.active].url

# 3. Sidebar.
Cmd 'sidebar 1'
$u = WaitFor { Ui } { param($u) $u.sidebar }
$kinds = ($u.rows | % kind) -join ','
Check 'sidebar opens with groups and bookmarks' ($u.sidebar -and $kinds -match '^header,group,panes,folder,folder') $kinds
Check 'own tabs listed as current' (($u.rows | ? kind -eq 'group')[0].current -and ($u.rows | ? kind -eq 'group')[0].title -eq '2 Tabs') (($u.rows | ? kind -eq 'group')[0].title)
Cmd 'sidebarpane r'
$u = Ui
Check 'Reading List pane' ($u.pane -eq 'r' -and @($u.rows | ? kind -eq 'reading').Count -eq 2) "pane=$($u.pane) rows=$(@($u.rows | ? kind -eq 'reading').Count)"
Shot 'm5-sidebar.png'
Cmd 'sidebarpane b'

# 4. Tab Groups.
Cmd 'newgroupwithtabs Research'
$u = WaitFor { Ui } { param($u) $u.group -ne 0 }
$research = $u.group
Check 'new group from the current tabs' ($research -ne 0 -and $u.label -eq 'Research' -and $u.ungrouped -eq 0) "group=$research label=$($u.label) ungrouped=$($u.ungrouped)"
Check 'group tabs saved' (($u.groups | ? id -eq $research).tabs -eq 2) (($u.groups | % { "$($_.name)=$($_.tabs)" }) -join ' ')
Cmd 'newgroup Trip'
$u = WaitFor { Ui } { param($u) $u.group -ne $research -and $u.group -ne 0 }
$trip = $u.group
$s = State
Check 'new empty group shows one start-page tab' ($s.tabs.Count -eq 1 -and $u.startpage) (Urls $s)
Cmd 'open https://example.org/'
$s = WaitFor { State } (Loaded 'example.org')
Cmd "group $research"
$s = WaitFor { State } { param($s) $s.group -eq $research -and $s.tabs.Count -eq 2 }
Check 'switching restores the group''s tabs' ($s.tabs.Count -eq 2 -and (Urls $s) -match 'wikipedia' -and (Urls $s) -match 'example.com') (Urls $s)
$u = Ui
Check 'the group left behind was saved' (($u.groups | ? id -eq $trip).tabs -eq 2) (($u.groups | % { "$($_.name)=$($_.tabs)" }) -join ' ')
Shot 'm5-groups.png'

Cmd 'group 0'
$s = WaitFor { State } { param($s) $s.group -eq 0 }
Check 'own tabs: empty after moving them into a group' ($s.tabs.Count -eq 1) (Urls $s)
Cmd 'open https://example.com/'
$s = WaitFor { State } (Loaded 'example.com')
Cmd "pin $($s.active)"
Cmd "group $research"
$s = WaitFor { State } { param($s) $s.group -eq $research }
Check 'pinned tabs stay across groups' ($s.tabs.Count -eq 3 -and $s.tabs[0].pinned -and $s.tabs[0].url -match 'example.com') (Urls $s)

Cmd "renamegroup $trip Holiday"
$u = Ui
Check 'rename a group' (($u.groups | ? id -eq $trip).name -eq 'Holiday') (($u.groups | % name) -join ', ')
Cmd "deletegroup $trip"
$u = Ui
Check 'delete a group' (-not ($u.groups | ? id -eq $trip)) (($u.groups | % name) -join ', ')

# 5. Restart: the window comes back showing its group, with its own tabs set aside.
Get-Process Meridian | ForEach-Object { [void]$_.CloseMainWindow() }
$t = [Diagnostics.Stopwatch]::StartNew(); while ((Windows).Count -and $t.Elapsed.TotalSeconds -lt 10) { Start-Sleep -Milliseconds 200 }
Launch $null
$s = WaitFor { State } { param($s) $s.tabs.Count -ge 1 }
$u = Ui
Check 'group restored after restart' ($s.group -eq $research -and $u.label -eq 'Research' -and $s.tabs.Count -eq 3) "group=$($s.group) label=$($u.label) $(Urls $s)"
Check 'set-aside tabs restored' ($u.stash -eq 1) "stash=$($u.stash)"
Check 'Reading List persists' ($u.reading.Count -eq 2) "items=$($u.reading.Count)"
Cmd 'group 0'
$s = WaitFor { State } { param($s) $s.group -eq 0 }
Check 'switch back to own tabs after restart' ($s.tabs.Count -eq 2 -and $s.tabs[0].pinned) (Urls $s)

Get-Process Meridian -ErrorAction SilentlyContinue | Stop-Process
Start-Sleep 1
if ($KeepProfile) { "profile kept: $profile" } else { Remove-Item -Recurse -Force $profile -ErrorAction SilentlyContinue }
$results | Format-Table -AutoSize -Wrap | Out-String -Width 220
if ($results.Result -contains 'FAIL') { exit 1 }
