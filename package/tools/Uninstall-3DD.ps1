param([string]$GamePath, [switch]$Quiet)
# Resident Evil 3DD Mod: removes the mod from the game folder (run <game>\3DD-Mod\Uninstall.cmd).
# Deletes the files listed in 3DD-Mod\installed-files.sha256 and what d3d9.dll wrote itself (runtime.log, captures,
# crash dumps), restores Width/Height in bio3.ini from 3DD-Mod\bio3.ini.original (other settings and saves stay as they
# are) and a d3d9.dll that was there before the mod, then deletes 3DD-Mod. The 3DD-Movies folder goes only when it is
# empty: movie files the player put there are kept.
$ErrorActionPreference = 'Stop'
$iniKeys = 'Width', 'Height'
$isRu = (Get-UICulture).TwoLetterISOLanguageName -in @('ru', 'uk', 'be', 'kk')

function L([string]$en, [string]$ru) { if ($isRu) { $ru } else { $en } }
function Say([string]$text, [string]$color = 'Gray') { Write-Host $text -ForegroundColor $color }
function Finish([int]$code) { if (-not $Quiet) { [void](Read-Host (L 'Press Enter to close this window' 'Нажмите Enter, чтобы закрыть окно')) }; exit $code }
function Fail([string]$text) { Say ''; Say ((L 'ERROR: ' 'ОШИБКА: ') + $text) Red; Finish 1 }
function IsGame([string]$path) { $path -and (Test-Path -LiteralPath (Join-Path $path 'ResidentEvil3.exe') -PathType Leaf) }
function Ask([string]$question) { if ($Quiet) { return $true }; $a = Read-Host "$question (Y/N)"; return $a -match '^\s*[YyДд]' }

try {
  if (-not $GamePath) { $GamePath = Split-Path $PSScriptRoot -Parent }
  if (-not (IsGame $GamePath)) { Fail (L "the game folder with ResidentEvil3.exe was not found ($GamePath)" "не найдена папка игры с ResidentEvil3.exe ($GamePath)") }
  $GamePath = (Resolve-Path -LiteralPath $GamePath).ProviderPath.TrimEnd('\')
  $modDir = Join-Path $GamePath '3DD-Mod'
  $manifestPath = Join-Path $modDir 'installed-files.sha256'
  if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { Fail (L "the mod is not installed in $GamePath (no 3DD-Mod\installed-files.sha256)" "мод в $GamePath не установлен (нет 3DD-Mod\installed-files.sha256)") }
  $version = Get-Content -LiteralPath (Join-Path $modDir 'version.txt') -TotalCount 1 -ErrorAction SilentlyContinue
  Say ("Resident Evil 3DD Mod $version - " + (L 'removal' 'удаление')) Cyan
  Say ((L 'Game folder: ' 'Папка игры: ') + $GamePath)
  $steamRoot = Split-Path $GamePath -Parent   # Steam: the mod is in <game folder>\english, the launcher one level up
  $running = @(Get-Process -Name ResidentEvil3, RE3_MERCE, RE3Launcher, '4249120_Launcher' -ErrorAction SilentlyContinue | Where-Object { $_.Path -and ($_.Path.StartsWith($GamePath, [StringComparison]::OrdinalIgnoreCase) -or
    ((Test-Path -LiteralPath (Join-Path $steamRoot '4249120_Launcher.exe')) -and $_.Path.StartsWith($steamRoot, [StringComparison]::OrdinalIgnoreCase))) })
  if ($running.Count) { Fail (L 'the game is running - close it and start the removal again' 'игра запущена - закройте её и запустите удаление снова') }
  if (-not (Ask (L 'Remove the mod from this folder? Saves and game files stay' 'Удалить мод из этой папки? Сохранения и файлы игры останутся'))) { Say (L 'Cancelled.' 'Отменено.'); Finish 0 }

  $writable = $true
  try { [IO.File]::WriteAllText((Join-Path $modDir 'write-test.tmp'), 'ok'); Remove-Item -LiteralPath (Join-Path $modDir 'write-test.tmp') }
  catch { $writable = $false; $why = $_.Exception.Message }
  if (-not $writable) {
    $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if ($admin -or $Quiet) { Fail (L "no write access to $GamePath ($why)" "нет права записи в $GamePath ($why)") }
    Say (L 'Administrator rights are needed - restarting the removal...' 'Нужны права администратора - перезапуск удаления...') Yellow
    $copy = Join-Path $env:TEMP 'RE3-3DD-Uninstall.ps1'; Copy-Item -LiteralPath $PSCommandPath -Destination $copy -Force
    Start-Process powershell.exe -Verb RunAs -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$copy`"", '-GamePath', "`"$GamePath`"")
    exit 0
  }

  Say (L 'Removing the mod files...' 'Удаление файлов мода...')
  $removed = 0
  foreach ($line in [IO.File]::ReadAllLines($manifestPath)) {
    if (-not $line.Trim()) { continue }
    $p = Join-Path $GamePath ($line.Split("`t")[2])
    if (Test-Path -LiteralPath $p -PathType Leaf) { Remove-Item -LiteralPath $p -Force; $removed++ }
  }
  foreach ($name in 'runtime.log', 'native-pose.json', 'native-pose-R1011A.json', 'dq-failed-pose.json') {
    $p = Join-Path $GamePath $name; if (Test-Path -LiteralPath $p -PathType Leaf) { Remove-Item -LiteralPath $p -Force }
  }
  foreach ($pair in @(@('captures', '*.tga'), @('crash-dumps', 're3-*.dmp'), @('ui-probes', '*'), @('pose-sequences', '*.json'))) {
    $dir = Join-Path $GamePath $pair[0]
    if (Test-Path -LiteralPath $dir -PathType Container) {
      Get-ChildItem -LiteralPath $dir -Filter $pair[1] -File -Recurse | Remove-Item -Force
      Get-ChildItem -LiteralPath $dir -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending | Where-Object { -not (Get-ChildItem -LiteralPath $_.FullName -Force) } | Remove-Item -Force
      if (-not (Get-ChildItem -LiteralPath $dir -Force)) { Remove-Item -LiteralPath $dir -Force }
    }
  }
  foreach ($top in 'pack', 'model-pack', 'ui-pack', '3DD-Movies') {   # 3DD-Movies: the player's own movies stay (controls round 2026-11-09)
    $dir = Join-Path $GamePath $top
    if (Test-Path -LiteralPath $dir -PathType Container) {
      Get-ChildItem -LiteralPath $dir -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending | Where-Object { -not (Get-ChildItem -LiteralPath $_.FullName -Force) } | Remove-Item -Force
      if (-not (Get-ChildItem -LiteralPath $dir -Force)) { Remove-Item -LiteralPath $dir -Force } else { Say (L "Folder $top was kept (it has other files)" "Оставлена папка $top (в ней есть чужие файлы)") Yellow }
    }
  }

  $iniPath = Join-Path $GamePath 'bio3.ini'; $iniBackup = Join-Path $modDir 'bio3.ini.original'
  if ((Test-Path -LiteralPath $iniPath) -and (Test-Path -LiteralPath $iniBackup)) {
    $ini = [IO.File]::ReadAllText($iniPath, [Text.Encoding]::Default); $original = [IO.File]::ReadAllText($iniBackup, [Text.Encoding]::Default)
    foreach ($k in $iniKeys) {
      $m = [regex]::Match($original, "(?m)^$k=[^\r\n]*")
      if ($m.Success) { $ini = [regex]::Replace($ini, "(?m)^$k=[^\r\n]*", $m.Value.Replace('$', '$$')) }
    }
    [IO.File]::WriteAllText($iniPath, $ini, [Text.Encoding]::Default)
  }
  $dllBackup = Join-Path $modDir 'd3d9.dll.original'
  if (Test-Path -LiteralPath $dllBackup) { Copy-Item -LiteralPath $dllBackup -Destination (Join-Path $GamePath 'd3d9.dll') -Force; Say (L 'The earlier d3d9.dll was restored' 'Восстановлен прежний d3d9.dll') }

  Set-Location -LiteralPath $GamePath
  Remove-Item -LiteralPath $modDir -Recurse -Force
  Say ''
  Say (L "Done: the mod is removed ($removed files), the game is back to its original files." "Готово: мод удалён (файлов: $removed), игра снова в исходном виде.") Green
  Finish 0
} catch { Fail $_.Exception.Message }
