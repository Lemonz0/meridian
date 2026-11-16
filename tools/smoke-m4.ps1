# M4 smoke test (Smart Search, start page, history, bookmarks): drives
# Meridian through its opt-in automation channel (--automation, WM_COPYDATA)
# in a throwaway profile and checks state dumps. Like smoke-m3, it never
# steals focus. It visits webkit.org and example.com, and (step 12) turns on
# search-engine suggestions in the throwaway profile, so DuckDuckGo receives
# the word "webkit". Pass -NoSearchSuggestions to skip that step.
param([string]$Exe = "$PSScriptRoot\..\build\bin\Meridian.exe", [string]$ShotDir = "$PSScriptRoot\..\docs\screenshots",
      [switch]$KeepProfile, [switch]$NoSearchSuggestions)

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
$uiDump = Join-Path $profile 'ui.txt'
$results = @()
function Check($name, $ok, $detail) { $script:results += [pscustomobject]@{ Test = $name; Result = $(if ($ok) { 'PASS' } else { 'FAIL' }); Detail = $detail } }
function Windows { @(Get-Process Meridian -ErrorAction SilentlyContinue | ForEach-Object { $_ }) }
function MainHwnd { [MA]::FindWindowEx([IntPtr]::Zero, [IntPtr]::Zero, 'MeridianBrowserWindow', [IntPtr]::Zero) }
function Cmd($c) { [void][MA]::Send((MainHwnd), $c) }
function ReadDump($cmd, $path) {
  # Commands run asynchronously (re-posted inside Meridian): delete, ask, wait.
  Remove-Item $path -ErrorAction SilentlyContinue
  Cmd "$cmd $path"
  $t = [Diagnostics.Stopwatch]::StartNew()
  while (-not (Test-Path $path) -and $t.Elapsed.TotalSeconds -lt 5) { Start-Sleep -Milliseconds 30 }
  if (Test-Path $path) { @(Get-Content $path -Encoding utf8) } else { @() }
}
function State {
  $s = [pscustomobject]@{ active = -1; tabs = @(); history = 0; favorites = 0; bookmarks = 0; suggest = $false }
  foreach ($l in (ReadDump 'dump' $dump)) {
    if ($l -match '^window 0 active=(-?\d+)') { $s.active = [int]$Matches[1] }
    elseif ($l -match '^tab (\d+) .* loading=(\d) .* url=(.*?) title=(.*)$') { $s.tabs += [pscustomobject]@{ loading = $Matches[2] -eq '1'; url = $Matches[3]; title = $Matches[4] } }
    elseif ($l -match '^data history=(\d+) favorites=(\d+) bookmarks=(\d+) suggest=(\d)') {
      $s.history = [int]$Matches[1]; $s.favorites = [int]$Matches[2]; $s.bookmarks = [int]$Matches[3]; $s.suggest = $Matches[4] -eq '1' }
  }
  $s
}
function Ui {
  $u = [pscustomobject]@{ editing = $false; selStart = 0; selEnd = 0; text = ''; typed = ''; popup = $false; selected = -1; items = @();
                          startpage = $false; fav = @(); freq = @(); historyOpen = $false; hist = @() }
  foreach ($l in (ReadDump 'dumpui' $uiDump)) {
    if ($l -match '^editing=(\d) sel=(\d+),(\d+)') { $u.editing = $Matches[1] -eq '1'; $u.selStart = [int]$Matches[2]; $u.selEnd = [int]$Matches[3] }
    elseif ($l -match '^text=(.*)$') { $u.text = $Matches[1] }
    elseif ($l -match '^typed=(.*)$') { $u.typed = $Matches[1] }
    elseif ($l -match '^popup visible=(\d) selected=(-?\d+)') { $u.popup = $Matches[1] -eq '1'; $u.selected = [int]$Matches[2] }
    elseif ($l -match '^item (\d+) kind=(\w+) history=(\d) bookmarked=(\d) url=(.*?) title=(.*)$') {
      $u.items += [pscustomobject]@{ kind = $Matches[2]; history = $Matches[3] -eq '1'; bookmarked = $Matches[4] -eq '1'; url = $Matches[5]; title = $Matches[6] } }
    elseif ($l -match '^startpage visible=(\d)') { $u.startpage = $Matches[1] -eq '1' }
    elseif ($l -match '^fav \d+ url=(.*?) title=') { $u.fav += $Matches[1] }
    elseif ($l -match '^freq \d+ url=(.*?) title=') { $u.freq += $Matches[1] }
    elseif ($l -match '^historyview open=(\d)') { $u.historyOpen = $Matches[1] -eq '1' }
    elseif ($l -match '^hist \d+ visits=(\d+) typed=(\d+) url=(.*?) title=(.*)$') { $u.hist += [pscustomobject]@{ visits = [int]$Matches[1]; typed = [int]$Matches[2]; url = $Matches[3]; title = $Matches[4] } }
  }
  $u
}
function WaitFor($get, $cond, $secs = 15) { $t = [Diagnostics.Stopwatch]::StartNew(); while ($t.Elapsed.TotalSeconds -lt $secs) { $s = & $get; if (& $cond $s) { return $s }; Start-Sleep -Milliseconds 250 }; & $get }
function Launch($url) {
  $a = @('--automation', "--profile=$profile"); if ($url) { $a += $url }
  Start-Process $Exe -ArgumentList $a | Out-Null
  $t = [Diagnostics.Stopwatch]::StartNew(); while (-not (MainHwnd) -and $t.Elapsed.TotalSeconds -lt 15) { Start-Sleep -Milliseconds 50 }
}
function Shot($name) { if ($ShotDir) { & "$PSScriptRoot\capture-window.ps1" -ProcessName Meridian -Screen -Out (Join-Path $ShotDir $name) 2>&1 | Out-Null } }
function ItemsText($u) { ($u.items | % { "$($_.kind):$($_.url)" }) -join ' | ' }
function Loaded($pattern) { { param($s) $s.active -ge 0 -and $s.tabs[$s.active].title -match $pattern -and -not $s.tabs[$s.active].loading }.GetNewClosure() }

Get-Process Meridian -ErrorAction SilentlyContinue | Stop-Process; Start-Sleep 1
New-Item -ItemType Directory -Force $profile | Out-Null

# 1. Visits are recorded.
Launch 'https://webkit.org/'
$s = WaitFor { State } (Loaded '^WebKit')
Check 'first page loads' ($s.tabs[$s.active].title -match 'WebKit') $s.tabs[$s.active].title
$s = WaitFor { State } { param($s) $s.history -ge 1 }
Check 'visit recorded in history' ($s.history -ge 1) "history=$($s.history)"
Check 'search suggestions off by default' (-not $s.suggest) "suggest=$($s.suggest)"
Cmd 'nav https://webkit.org/blog/'; $s = WaitFor { State } (Loaded 'Blog')
Cmd 'nav https://example.com/';     $s = WaitFor { State } (Loaded 'Example Domain')
Cmd 'nav https://webkit.org/';      $s = WaitFor { State } (Loaded '^WebKit')
$s = WaitFor { State } { param($s) $s.history -ge 3 }
Check 'one history row per URL' ($s.history -eq 3) "history=$($s.history)"

# 2. New tab shows the start page; Frequently Visited has webkit.org (2 visits).
Cmd 'open about:blank'
$u = WaitFor { Ui } { param($u) $u.startpage }
Check 'new tab shows the start page' ($u.startpage) "startpage=$($u.startpage)"
Check 'frequently visited lists webkit.org' ($u.freq -contains 'https://webkit.org/') ($u.freq -join ' ')
Check 'pages visited once are not frequent' (-not ($u.freq -contains 'https://example.com/')) ($u.freq -join ' ')

# 3. Smart Search: Top Hit + inline autocomplete.
Cmd 'type web'
$u = WaitFor { Ui } { param($u) $u.popup }
Check 'dropdown opens while typing' ($u.popup -and $u.items.Count -ge 2) (ItemsText $u)
Check 'inline autocomplete to the host' ($u.text -eq 'webkit.org' -and $u.selStart -eq 3 -and $u.selEnd -eq 10) "text='$($u.text)' sel=$($u.selStart),$($u.selEnd)"
Check 'Top Hit is the site, selected' ($u.items[0].kind -eq 'tophit' -and $u.items[0].url -eq 'https://webkit.org/' -and $u.selected -eq 0) (ItemsText $u)
Check 'search row offered' (@($u.items | ? kind -eq 'search').Count -eq 1) (ItemsText $u)
Shot 'm4-smart-search.png'

Cmd 'key back'
$u = Ui
Check 'Backspace removes the completion' ($u.text -eq 'web' -and $u.selected -eq -1) "text='$($u.text)' selected=$($u.selected)"

Cmd 'type webkit.org/b'
$u = Ui
Check 'path completion after a slash' ($u.text -eq 'webkit.org/blog/' -and $u.items[0].url -eq 'https://webkit.org/blog/') "text='$($u.text)' $(ItemsText $u)"

Cmd 'key down'; $u = Ui
Check 'Down moves the selection' ($u.selected -eq 1) "selected=$($u.selected) text='$($u.text)'"
Cmd 'key up'; Cmd 'key up'; $u = Ui
Check 'Up past the first row returns to the typed text' ($u.selected -eq -1 -and $u.text -eq 'webkit.org/b') "selected=$($u.selected) text='$($u.text)'"
Cmd 'key esc'; $u = Ui
Check 'Escape closes the dropdown, keeps the text' (-not $u.popup -and $u.editing -and $u.text -eq 'webkit.org/b') "popup=$($u.popup) text='$($u.text)'"
Cmd 'key esc'; $u = Ui
Check 'second Escape ends editing' (-not $u.editing) "editing=$($u.editing)"

Cmd 'type hello meridian'; $u = Ui
Check 'several words: search, no completion' ($u.items[0].kind -eq 'search' -and $u.text -eq 'hello meridian') (ItemsText $u)
Cmd 'key esc'; Cmd 'key esc'

# 4. Enter on the autocompleted Top Hit navigates (and counts as typed).
Cmd 'type exa'; $u = Ui
Check 'completes example.com' ($u.text -eq 'example.com') "text='$($u.text)'"
Cmd 'key enter'
$s = WaitFor { State } (Loaded 'Example Domain')
Check 'Enter opens the Top Hit in the tab' ($s.tabs[$s.active].url -match 'example.com') $s.tabs[$s.active].url
Check 'start page gone after navigating' (-not (Ui).startpage) ''

# 5. Ctrl+D adds to Favorites; start page shows it; clicking the tile navigates.
Cmd 'bookmark'
$s = WaitFor { State } { param($s) $s.favorites -eq 1 }
Check 'Ctrl+D adds the page to Favorites' ($s.favorites -eq 1) "favorites=$($s.favorites)"
Cmd 'bookmark'; $s = State
Check 'Ctrl+D twice does not duplicate' ($s.favorites -eq 1) "favorites=$($s.favorites)"
Cmd 'open about:blank'
$u = WaitFor { Ui } { param($u) $u.startpage -and $u.fav.Count -ge 1 }
Check 'Favorites on the start page' ($u.fav -contains 'https://example.com/') ($u.fav -join ' ')
Check 'favorites are not repeated in Frequently Visited' (-not ($u.freq -contains 'https://example.com/')) ($u.freq -join ' ')
Shot 'm4-start-page.png'
Cmd 'starttile f 0'
$s = WaitFor { State } (Loaded 'Example Domain')
Check 'favorite tile opens the page' ($s.tabs[$s.active].url -match 'example.com') $s.tabs[$s.active].url

# 6. Shift+Delete removes a history row from the dropdown.
Cmd 'type webkit.org/bl'; $u = Ui
$blog = [array]::IndexOf(@($u.items | % url), 'https://webkit.org/blog/')
Check 'history page is suggested' ($blog -ge 0 -and $u.items[$blog].history) (ItemsText $u)
Cmd 'key shiftdel'; $u = Ui
Check 'Shift+Delete removes it' (-not (@($u.items | % url) -contains 'https://webkit.org/blog/')) (ItemsText $u)
Cmd 'key esc'; Cmd 'key esc'
$s = State
Check 'removed from the database' ($s.history -eq 2) "history=$($s.history)"

# 7. History view: search, typed counts, delete.
Cmd 'history 1'
$u = WaitFor { Ui } { param($u) $u.historyOpen }
Check 'History view opens' ($u.historyOpen -and $u.hist.Count -eq 2) (($u.hist | % url) -join ' ')
$ex = $u.hist | ? url -eq 'https://example.com/'
Check 'typed visits counted' ($ex -and $ex.typed -ge 1 -and $ex.visits -ge 3) "example.com visits=$($ex.visits) typed=$($ex.typed)"
Shot 'm4-history.png'
Cmd 'historysearch example'; $u = Ui
Check 'history search filters' ($u.hist.Count -eq 1 -and $u.hist[0].url -eq 'https://example.com/') (($u.hist | % url) -join ' ')
Cmd 'historysearch '
Cmd 'historydelete 0'; $u = Ui
Check 'delete from History view' ($u.hist.Count -eq 1) (($u.hist | % url) -join ' ')
Cmd 'history 0'; $u = Ui
Check 'History view closes' (-not $u.historyOpen) ''

# 8. Bookmark export / import (Netscape HTML).
$export = Join-Path $profile 'export.html'
Cmd "exportbookmarks $export"
$t = [Diagnostics.Stopwatch]::StartNew(); while (-not (Test-Path $export) -and $t.Elapsed.TotalSeconds -lt 5) { Start-Sleep -Milliseconds 50 }
$html = if (Test-Path $export) { Get-Content $export -Raw -Encoding utf8 } else { '' }
Check 'export writes a bookmarks file' ($html -match 'NETSCAPE-Bookmark-file-1' -and $html -match 'https://example.com/') "$($html.Length) bytes"
$import = Join-Path $profile 'import.html'
@'
<!DOCTYPE NETSCAPE-Bookmark-file-1>
<DL><p>
  <DT><H3 PERSONAL_TOOLBAR_FOLDER="true">Bookmarks bar</H3>
  <DL><p><DT><A HREF="https://www.wikipedia.org/">Wikipedia</A></DL><p>
  <DT><H3>Work</H3>
  <DL><p><DT><A HREF="https://bugs.webkit.org/">WebKit Bugzilla</A><DT><A HREF="javascript:alert(1)">bad</A></DL><p>
</DL><p>
'@ | Set-Content -Encoding utf8 $import
Cmd "importbookmarks $import"
$s = WaitFor { State } { param($s) $s.favorites -eq 2 }
Check 'import: toolbar folder merges into Favorites' ($s.favorites -eq 2) "favorites=$($s.favorites)"
Check 'import: folders kept, javascript: links dropped' ($s.bookmarks -eq 1) "top-level bookmarks=$($s.bookmarks) (the Work folder)"
Cmd 'type wiki'; $u = Ui
Check 'imported bookmark is suggested' ($u.text -eq 'wikipedia.org' -and $u.items[0].bookmarked) (ItemsText $u)
Cmd 'key esc'; Cmd 'key esc'

# 9. Opt-in search-engine suggestions (network: DuckDuckGo receives "webkit").
if (-not $NoSearchSuggestions) {
  Cmd 'type webkit'; Start-Sleep 2; $u = Ui
  Check 'no engine suggestions while off' (@($u.items | ? kind -eq 'searchsuggestion').Count -eq 0) (ItemsText $u)
  Cmd 'key esc'; Cmd 'key esc'
  Cmd 'setting suggest 1'
  Cmd 'type webkit'
  $u = WaitFor { Ui } { param($u) @($u.items | ? kind -eq 'searchsuggestion').Count -gt 0 } 8
  Check 'engine suggestions after opting in' (@($u.items | ? kind -eq 'searchsuggestion').Count -gt 0) (ItemsText $u)
  Cmd 'key esc'; Cmd 'key esc'
  Cmd 'setting suggest 0'
}

# 10. Everything survives a restart.
Get-Process Meridian | ForEach-Object { [void]$_.CloseMainWindow() }
$t = [Diagnostics.Stopwatch]::StartNew(); while ((Windows).Count -and $t.Elapsed.TotalSeconds -lt 10) { Start-Sleep -Milliseconds 200 }
Launch $null
$s = WaitFor { State } { param($s) $s.tabs.Count -ge 1 -and $s.history -ge 1 }
Check 'history and bookmarks persist across restart' ($s.history -eq 1 -and $s.favorites -eq 2) "history=$($s.history) favorites=$($s.favorites)"
Start-Sleep 2; $s2 = State
Check 'restoring the session is not a new visit' ($s2.history -eq $s.history) "before=$($s.history) after=$($s2.history)"

Get-Process Meridian -ErrorAction SilentlyContinue | Stop-Process
Start-Sleep 1
if ($KeepProfile) { "profile kept: $profile" } else { Remove-Item -Recurse -Force $profile -ErrorAction SilentlyContinue }
$results | Format-Table -AutoSize -Wrap | Out-String -Width 220
if ($results.Result -contains 'FAIL') { exit 1 }
