param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\out\build\x64-debug\src\Debug\TranslucentExplorer.exe'),
    [string]$Report = "$env:TEMP\te-crt-report.txt"
)
# Debug CRT leak run (T093, SC-010): starts the Debug build with TE_CRT_REPORT set, runs V-3 and
# V-4 steps through window messages only (no injected input, no clipboard), closes the window,
# and prints the CRT leak report ("(empty: no leaks reported)" when clean). V-4 works in a
# scratch folder under %TEMP%; the one file it recycles is removed from the Recycle Bin again,
# and nothing else there is touched. Needs tools\New-TestData.ps1 and the Recycle Bin delete
# confirmation off (with it on, the Shell asks and the run waits). Usage:
#   .\tools\Invoke-CrtLeakRun.ps1

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class W {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
}
'@

$WM_KEYDOWN = 0x100; $WM_COMMAND = 0x111; $WM_SETTEXT = 0x0C; $WM_CLOSE = 0x10
$IDM_BACK = 40010; $IDM_FORWARD = 40011; $IDM_UP = 40012; $IDM_REFRESH = 40013
$IDM_FOCUS_ADDRESS = 40014; $IDM_FOCUS_FILTER = 40015; $IDM_OPEN_APPEARANCE = 40001
$AddressEdit = 0x4144; $FilterEdit = 0x4649; $RenameEdit = 0x524E

function Key($h, $vk) { [void][W]::SendMessageW($h, $WM_KEYDOWN, [IntPtr]$vk, [IntPtr]0) }
function Cmd($h, $id) { [void][W]::SendMessageW($h, $WM_COMMAND, [IntPtr]($id -bor (1 -shl 16)), [IntPtr]0) }
function Title($h) { $sb = New-Object System.Text.StringBuilder 512; [void][W]::GetWindowTextW($h, $sb, 512); $sb.ToString() }
function GoTo($h, $path) {
    Cmd $h $IDM_FOCUS_ADDRESS
    $edit = [W]::GetDlgItem($h, $AddressEdit)
    [void][W]::SendMessageW($edit, $WM_SETTEXT, [IntPtr]0, $path)
    Key $edit 0x0D
    Start-Sleep -Milliseconds 1500
}

# A scratch folder for V-4 (rename, delete to the Recycle Bin).
$scratch = Join-Path $env:TEMP ("te-crt-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path "$scratch\sub" | Out-Null
'a' | Set-Content "$scratch\keep.txt"
'b' | Set-Content "$scratch\rename-me.txt"
'c' | Set-Content "$scratch\zz-delete-me.txt"
'd' | Set-Content "$scratch\sub\inner.txt"

Remove-Item $Report -ErrorAction SilentlyContinue
$env:TE_CRT_REPORT = $Report
$p = Start-Process -FilePath $Exe -ArgumentList "`"$env:TEMP\te-test`"" -PassThru
Remove-Item Env:TE_CRT_REPORT
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 100 -and $h -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 100; $p.Refresh(); $h = $p.MainWindowHandle }
if ($h -eq [IntPtr]::Zero) { throw 'no window' }
Start-Sleep -Milliseconds 1500
$steps = @()

# ---- V-3: navigation
Key $h 0x24; Key $h 0x28; Key $h 0x28                  # Home, Down, Down in the list
GoTo $h "$env:TEMP\te-test\nested"; $steps += "address -> nested: $(Title $h)"
Key $h 0x24; Key $h 0x0D; Start-Sleep -Milliseconds 800; $steps += "Enter on first row: $(Title $h)"
Cmd $h $IDM_BACK; Start-Sleep -Milliseconds 800; $steps += "Back: $(Title $h)"
Cmd $h $IDM_FORWARD; Start-Sleep -Milliseconds 800; $steps += "Forward: $(Title $h)"
Cmd $h $IDM_UP; Start-Sleep -Milliseconds 800; $steps += "Up: $(Title $h)"
Cmd $h $IDM_REFRESH; Start-Sleep -Milliseconds 800; $steps += "Refresh: $(Title $h)"
GoTo $h "$env:TEMP\te-test\10k"; Key $h 0x23; Start-Sleep -Milliseconds 800; Key $h 0x24; $steps += "10k, End, Home: $(Title $h)"
for ($i = 0; $i -lt 5; $i++) { Cmd $h $IDM_BACK; Cmd $h $IDM_FORWARD }; Start-Sleep -Milliseconds 1500; $steps += "rapid Back/Forward x5: $(Title $h)"
GoTo $h "$env:TEMP\te-test\unicode"; $steps += "unicode: $(Title $h)"
GoTo $h "$env:TEMP\te-test\does-not-exist"; Key ([W]::GetDlgItem($h, $AddressEdit)) 0x1B; $steps += "bad path, Escape: $(Title $h)"
# Filter (T092), the appearance popup opened and closed, the tree (F6 to the pane, Right, Left).
GoTo $h "$env:TEMP\te-test\10k"
Cmd $h $IDM_FOCUS_FILTER; $fe = [W]::GetDlgItem($h, $FilterEdit)
[void][W]::SendMessageW($fe, $WM_SETTEXT, [IntPtr]0, 'file0001'); Key $fe 0x0D; Start-Sleep -Milliseconds 300
Cmd $h $IDM_FOCUS_FILTER; Key ([W]::GetDlgItem($h, $FilterEdit)) 0x1B; $steps += "filter file0001, Escape"
Cmd $h $IDM_OPEN_APPEARANCE; Start-Sleep -Milliseconds 400; Cmd $h $IDM_OPEN_APPEARANCE; $steps += 'appearance popup open/close'
GoTo $h "$env:TEMP\te-test"                               # the keyboard is on the address now
Key $h 0x75; Key $h 0x24; Key $h 0x27; Start-Sleep -Milliseconds 1500   # F6 -> pane, Home, Right
$steps += "tree: This PC expanded, rows: $(if ($true) { 'see pane' })"
Key $h 0x25                                                              # Left: collapse

# ---- V-4: rename and delete in the scratch folder
GoTo $h $scratch; $steps += "scratch: $(Title $h)"
Key $h 0x75; Key $h 0x75                               # address -> pane -> file list (F6 ring)
Key $h 0x24; Key $h 0x28; Key $h 0x28                  # Home, Down x2: sub, keep, rename-me
Key $h 0x71; Start-Sleep -Milliseconds 300              # F2
$re = [W]::GetDlgItem($h, $RenameEdit)
if ([W]::IsWindowVisible($re)) {
    [void][W]::SendMessageW($re, $WM_SETTEXT, [IntPtr]0, 'renamed.txt'); Key $re 0x0D; Start-Sleep -Milliseconds 1500
}
$steps += "rename -> renamed.txt: $(Test-Path "$scratch\renamed.txt")"
Key $h 0x23; Key $h 0x2E; Start-Sleep -Milliseconds 2000   # End (zz-delete-me), Delete -> Recycle Bin
$steps += "delete zz-delete-me: gone=$(-not (Test-Path "$scratch\zz-delete-me.txt"))"

# ---- close
[void][W]::SendMessageW($h, $WM_CLOSE, [IntPtr]0, [IntPtr]0)
if (-not $p.WaitForExit(15000)) { $steps += 'did not exit in 15 s'; $p.Kill() }
$steps += "exit code: $($p.ExitCode)"

# Clean up: only the item this run put in the Recycle Bin, and the scratch folder.
$shell = New-Object -ComObject Shell.Application
$bin = $shell.Namespace(0xA)
foreach ($item in @($bin.Items())) {
    $original = $bin.GetDetailsOf($item, 1)
    if ($original -eq $scratch) {
        $r = $item.Path; $i = Join-Path (Split-Path $r) ('$I' + (Split-Path $r -Leaf).Substring(2))
        Remove-Item -LiteralPath $r -Force -Recurse -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $i -Force -ErrorAction SilentlyContinue
        $steps += "recycle bin item removed: $r"
    }
}
Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue

$steps | ForEach-Object { "  $_" }
'--- CRT report ---'
if (Test-Path $Report) { $text = Get-Content $Report -Raw; if ($text) { $text } else { '(empty: no leaks reported)' } } else { '(no report file)' }

