# OpenGTA @VERSION@ (opengta.wasm) on the bundled gasm-run @GASM_VERSION@: Windows launcher, started by OpenGTA.cmd.
#   OpenGTA.cmd [options] [DATA] [gasm-run options...]          (OpenGTA.cmd --help)
# The game data location comes from the argument (then saved), else %APPDATA%\OpenGTA\data-location, else a
# folder picker. gasm-run's output goes to the console and to %LOCALAPPDATA%\OpenGTA\gasm.log. Test hooks (no
# dialogs): OPENGTA_DATA=<folder> uses that data without saving it (and turns missing data into exit 2),
# OPENGTA_DRY_RUN=1 or --dry-run prints the gasm-run command instead of running it.
$ErrorActionPreference = 'Stop'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$ConfDir = Join-Path $env:APPDATA 'OpenGTA'
$LocFile = Join-Path $ConfDir 'data-location'
$LogDir = Join-Path $(if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { $env:APPDATA }) 'OpenGTA'
$Log = Join-Path $LogDir 'gasm.log'
$Title = 'OpenGTA (gasm)'
$ExeSize = 774144   # WINO\Grand Theft Auto.exe of Rockstar's 2002 re-release
$Dry = $env:OPENGTA_DRY_RUN -eq '1'
$Change = $false

function Show-Usage {
  @"
OpenGTA @VERSION@ on gasm-run @GASM_VERSION@

  OpenGTA.cmd [options] [DATA] [gasm-run options...]

DATA is your copy of GTA from Rockstar's 2002 re-release (GTAINSTALLER.zip): the installed game's folder
(it has GTADATA\ and WINO\Grand Theft Auto.exe) or the unzipped installer (the folder with data1.cab and
data2.cab). It is saved in
  $LocFile
so later runs need no argument. Without one, a dialog asks for it.

Options:
  --change-data  ask for the game data even if a location is saved
  --forget-data  delete the saved location and exit
  --dry-run      print the gasm-run command instead of running it (also OPENGTA_DRY_RUN=1)
  --help         this text
Anything after DATA goes to gasm-run, e.g. --param intro=0, --window 1280x960, --filter nearest, --mute.
OPENGTA_DATA=<DATA> uses that data for one run without saving it.

Log: $Log
More: https://opengta.emdzej.pl/guide/install
"@
}

# $null if usable, else the reason (Windows paths are case-insensitive)
function Test-Data([string]$p) {
  if (Test-Path -LiteralPath $p -PathType Leaf) {
    if ([IO.Path]::GetExtension($p).ToLowerInvariant() -eq '.zip') {
      return 'This is the zip: unzip GTAINSTALLER.zip first, then choose the folder it makes.'
    }
    return "This is a file: choose the GTA folder (or the unzipped installer's folder).`n$p"
  }
  if (-not (Test-Path -LiteralPath $p -PathType Container)) {
    return "The GTA game data was not found at:`n$p`nChoose it again."
  }
  $gd = Join-Path $p 'GTADATA'; $wino = Join-Path $p 'WINO'
  if ((Test-Path -LiteralPath $gd -PathType Container) -or (Test-Path -LiteralPath $wino -PathType Container)) {
    if (-not (Test-Path -LiteralPath (Join-Path $gd 'MISSION.INI') -PathType Leaf)) {
      return "This GTA folder has no GTADATA\MISSION.INI:`n$p"
    }
    $exe = Join-Path $wino 'Grand Theft Auto.exe'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { return "This GTA folder has no WINO\Grand Theft Auto.exe:`n$p" }
    $size = (Get-Item -LiteralPath $exe).Length
    if ($size -ne $ExeSize) {
      return "WINO\Grand Theft Auto.exe is $size bytes, not 774,144: OpenGTA needs the Windows game of Rockstar's 2002 re-release (GTAINSTALLER.zip).`n$p"
    }
    return $null
  }
  $c1 = Test-Path -LiteralPath (Join-Path $p 'data1.cab') -PathType Leaf
  $c2 = Test-Path -LiteralPath (Join-Path $p 'data2.cab') -PathType Leaf
  if ($c1 -and $c2) { return $null }
  if ($c1 -or $c2) { return "This installer folder needs both data1.cab and data2.cab:`n$p" }
  return "This folder is neither the installed GTA (it has GTADATA and WINO) nor the unzipped installer (it has data1.cab and data2.cab):`n$p"
}

function Get-Absolute([string]$p) {
  $full = [IO.Path]::GetFullPath([IO.Path]::Combine((Get-Location).ProviderPath, $p))
  if ($full.Length -gt 3) { $full = $full.TrimEnd('\') }   # keep D:\ as it is
  return $full
}

# The message with OK / Quit, then the folder picker. Returns the path or $null.
function Request-Data([string]$message) {
  Add-Type -AssemblyName System.Windows.Forms
  [System.Windows.Forms.Application]::EnableVisualStyles()
  $answer = [System.Windows.Forms.MessageBox]::Show("$message`n`nOK: choose your GTA folder. Cancel: quit.", $Title,
    [System.Windows.Forms.MessageBoxButtons]::OKCancel, [System.Windows.Forms.MessageBoxIcon]::Information)
  if ($answer -ne [System.Windows.Forms.DialogResult]::OK) { return $null }
  $d = New-Object System.Windows.Forms.FolderBrowserDialog
  $d.Description = 'Choose the installed GTA (the folder with GTADATA and WINO) or the unzipped installer (the folder with data1.cab)'
  $d.ShowNewFolderButton = $false
  if ($d.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { return $d.SelectedPath }
  return $null
}

$Intro = @"
OpenGTA needs your copy of GTA (it is not included): Rockstar's 2002 re-release, GTAINSTALLER.zip.

Choose the folder of the installed game (it has GTADATA and WINO, e.g. C:\Program Files (x86)\Rockstar Games\GTA), or the folder you unzipped GTAINSTALLER.zip to (it has data1.cab and data2.cab; no need to run its setup).

Your choice is remembered; run OpenGTA.cmd --change-data to pick another.
"@

$rest = @($args)
while ($rest.Count -gt 0) {
  $a = [string]$rest[0]
  if ($a -eq '--help' -or $a -eq '-h' -or $a -eq '/?') { Show-Usage; exit 0 }
  elseif ($a -eq '--dry-run') { $Dry = $true }
  elseif ($a -eq '--change-data') { $Change = $true }
  elseif ($a -eq '--forget-data') {
    Remove-Item -LiteralPath $LocFile -ErrorAction SilentlyContinue; "forgot the game data location ($LocFile)"; exit 0
  }
  else { break }
  $rest = @($rest | Select-Object -Skip 1)
}
$data = $null; $save = $false
if ($rest.Count -gt 0 -and -not ([string]$rest[0]).StartsWith('-')) {
  $data = Get-Absolute ([string]$rest[0]); $save = $true; $rest = @($rest | Select-Object -Skip 1)
}
if (-not $data -and $env:OPENGTA_DATA) { $data = Get-Absolute $env:OPENGTA_DATA }
if (-not $data -and -not $Change -and (Test-Path -LiteralPath $LocFile)) {
  $data = (Get-Content -LiteralPath $LocFile -TotalCount 1).Trim()
}

$msg = $Intro
if ($data) { $msg = Test-Data $data }
while (-not $data -or (Test-Data $data)) {
  if ($Dry -or $env:OPENGTA_DATA) {   # never open dialogs in test runs
    if ($data) { [Console]::Error.WriteLine("no usable GTA game data: $msg") } else { [Console]::Error.WriteLine('no usable GTA game data') }
    exit 2
  }
  $data = Request-Data $msg
  if (-not $data) { exit 0 }
  $data = Get-Absolute $data; $save = $true
  $msg = Test-Data $data
}
if ($save -and -not $Dry) {
  New-Item -ItemType Directory -Force -Path $ConfDir | Out-Null
  Set-Content -LiteralPath $LocFile -Value $data -Encoding UTF8
}

$run = Join-Path $Here 'gasm-run.exe'
$cmd = @((Join-Path $Here 'opengta.wasm'), '--asset-dir', $data) + $rest
if ($Dry) { (@($run) + $cmd | ForEach-Object { '"' + $_ + '"' }) -join ' '; exit 0 }

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
Add-Content -LiteralPath $Log -Value ("--- {0} OpenGTA @VERSION@, gasm-run @GASM_VERSION@" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Add-Content -LiteralPath $Log -Value ((@($run) + $cmd | ForEach-Object { '"' + $_ + '"' }) -join ' ')
# gasm-run's stderr lines arrive as error records: keep going, and write them out as plain text.
$ErrorActionPreference = 'Continue'
& $run @cmd 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $Log -Append
exit $LASTEXITCODE
