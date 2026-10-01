<#
.SYNOPSIS
    Creates (or removes) the Translucent Explorer test-data tree used by the quickstart
    validation scenarios (V-3, V-4, V-6) and the integration tests.

.DESCRIPTION
    Creates under -Root:
      nested\a\b\c      a few .txt files at each level
      10k\              10,000 zero-byte files, file00001.txt ... file10000.txt
      conflicts\src     5 files with the same names as conflicts\dst, different content
      conflicts\dst
      readonly-acl\     list and write access denied to the current user (icacls)
      unicode\          file and folder names with emoji, CJK, Hebrew and Arabic text
      longpath\         a folder chain deeper than 260 characters (\\?\ paths)
      unknown.zzz       a file with no associated application

    The script is idempotent: running it again only creates what is missing.
    -Remove restores the ACL on readonly-acl and deletes the whole tree. It only deletes
    a folder that contains the marker file this script writes, so a wrong -Root cannot
    delete unrelated data.

    Works in Windows PowerShell 5.1 and PowerShell 7. This file is ASCII only; non-ASCII
    names are built from code points.

.PARAMETER Root
    Folder to create. Default: %TEMP%\te-test

.PARAMETER Remove
    Restore permissions and delete the tree instead of creating it.

.EXAMPLE
    .\tools\New-TestData.ps1 -Root "$env:TEMP\te-test"

.EXAMPLE
    .\tools\New-TestData.ps1 -Root "$env:TEMP\te-test" -Remove
#>
[CmdletBinding()]
param(
    [string] $Root = (Join-Path $env:TEMP 'te-test'),
    [switch] $Remove
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$MarkerName = '.te-test-data'
$Root = [System.IO.Path]::GetFullPath($Root)
$UserSid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value

function Get-LongPath([string] $Path) {
    # \\?\ lets .NET and Win32 handle paths longer than MAX_PATH (260).
    if ($Path.StartsWith('\\?\')) { return $Path }
    return '\\?\' + $Path
}

function New-Folder([string] $Path) {
    [void][System.IO.Directory]::CreateDirectory((Get-LongPath $Path))
}

function New-TextFile([string] $Path, [string] $Content) {
    if (-not [System.IO.File]::Exists((Get-LongPath $Path))) {
        [System.IO.File]::WriteAllText((Get-LongPath $Path), $Content, [System.Text.UTF8Encoding]::new($false))
    }
}

function Invoke-Icacls([string[]] $Arguments) {
    $output = & icacls.exe @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "icacls $($Arguments -join ' ') failed ($LASTEXITCODE): $output"
    }
}

function U([int[]] $CodePoints) {
    # Builds a string from Unicode code points (supports characters above U+FFFF).
    return -join ($CodePoints | ForEach-Object { [char]::ConvertFromUtf32($_) })
}

# ---------------------------------------------------------------------------
# -Remove
# ---------------------------------------------------------------------------
if ($Remove) {
    if (-not (Test-Path -LiteralPath $Root)) {
        Write-Host "Nothing to remove: $Root does not exist."
        return
    }
    if (-not (Test-Path -LiteralPath (Join-Path $Root $MarkerName))) {
        throw "Refusing to delete '$Root': it does not contain the marker file '$MarkerName' written by this script."
    }
    $acl = Join-Path $Root 'readonly-acl'
    if (Test-Path -LiteralPath $acl) {
        Invoke-Icacls @($acl, '/remove:d', "*$UserSid", '/T', '/C', '/Q')
    }
    [System.IO.Directory]::Delete((Get-LongPath $Root), $true)
    Write-Host "Removed $Root"
    return
}

# ---------------------------------------------------------------------------
# Create
# ---------------------------------------------------------------------------
$started = Get-Date
New-Folder $Root
New-TextFile (Join-Path $Root $MarkerName) "Created by tools/New-TestData.ps1. -Remove deletes this folder."

# nested\a\b\c
$level = $Root
foreach ($name in 'nested', 'a', 'b', 'c') {
    $level = Join-Path $level $name
    New-Folder $level
    New-TextFile (Join-Path $level "readme-$name.txt") "Level $name"
    New-TextFile (Join-Path $level "notes-$name.txt") "Notes for level $name"
}

# 10k\ : 10,000 zero-byte files
$tenK = Join-Path $Root '10k'
New-Folder $tenK
$existing = [System.IO.Directory]::GetFiles($tenK).Count
if ($existing -lt 10000) {
    $empty = [byte[]]::new(0)
    for ($i = 1; $i -le 10000; $i++) {
        $file = Join-Path $tenK ('file{0:D5}.txt' -f $i)
        if (-not [System.IO.File]::Exists($file)) {
            [System.IO.File]::WriteAllBytes($file, $empty)
        }
    }
}

# conflicts\src and conflicts\dst: same names, different content
$src = Join-Path $Root 'conflicts\src'
$dst = Join-Path $Root 'conflicts\dst'
New-Folder $src
New-Folder $dst
for ($i = 1; $i -le 5; $i++) {
    New-TextFile (Join-Path $src "conflict$i.txt") "SOURCE version of conflict$i"
    New-TextFile (Join-Path $dst "conflict$i.txt") "DESTINATION version of conflict$i (must survive a skipped copy)"
}

# readonly-acl\ : contents first, then deny list and write to the current user
$acl = Join-Path $Root 'readonly-acl'
if (-not (Test-Path -LiteralPath $acl)) {
    New-Folder $acl
    New-TextFile (Join-Path $acl 'secret.txt') 'You should not be able to list or change this folder.'
    # RD = list folder / read data, WD = create files / write data, AD = create folders.
    Invoke-Icacls @($acl, '/deny', "*${UserSid}:(OI)(CI)(RD,WD,AD)", '/Q')
}

# unicode\ : emoji, CJK, Hebrew (RTL) and Arabic (RTL) names
$unicode = Join-Path $Root 'unicode'
New-Folder $unicode
$names = @(
    ('emoji-' + (U 0x1F4C1, 0x1F680) + '.txt'),                    # folder, rocket
    ('cjk-' + (U 0x6587, 0x4EF6, 0x540D) + '.txt'),                 # Chinese "file name"
    ('hebrew-' + (U 0x05E7, 0x05D5, 0x05D1, 0x05E5) + '.txt'),      # Hebrew "file"
    ('arabic-' + (U 0x0645, 0x0644, 0x0641) + '.txt'),              # Arabic "file"
    ('accents-r' + (U 0x00E9) + 'sum' + (U 0x00E9) + '.txt')        # resume with accents
)
foreach ($name in $names) {
    New-TextFile (Join-Path $unicode $name) "Unicode name test: $name"
}
New-Folder (Join-Path $unicode ('folder-' + (U 0x6587, 0x4EF6, 0x5939) + '-' + (U 0x1F4C2)))

# longpath\ : a folder chain deeper than 260 characters
$segment = 'segment-' + ('x' * 40)
$deep = Join-Path $Root 'longpath'
while ($deep.Length -le 300) {
    $deep = Join-Path $deep $segment
}
New-Folder $deep
New-TextFile (Join-Path $deep 'deep-file.txt') "This file's full path is longer than 260 characters."

# unknown.zzz : no associated application
New-TextFile (Join-Path $Root 'unknown.zzz') 'A file with an extension no application is registered for.'

$deepFile = Join-Path $deep 'deep-file.txt'
Write-Host ("Test data ready in {0} ({1:N1} s)" -f $Root, ((Get-Date) - $started).TotalSeconds)
Write-Host ("  10k files: {0}; deepest path length: {1}" -f [System.IO.Directory]::GetFiles($tenK).Count, $deepFile.Length)
