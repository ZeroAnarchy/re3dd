param([string]$GamePath, [switch]$Quiet)
# Resident Evil 3DD Mod: installs the mod over the GOG version (1.0 hotfix4) of Resident Evil 3.
#   Install.cmd                     - finds the game (the folder above the patch, GOG registry, folder dialog), asks, installs
#   Install.cmd "D:\Games\RE3"      - installs into the given folder
# Copies files\* into the game folder, checks every file against tools\files.sha256 and sets bio3.ini Width/Height to
# 2560x1920 (the game keeps its own movies, Movie= is not touched). The original bio3.ini (and a foreign d3d9.dll, if any)
# is kept in <game>\3DD-Mod, together with Uninstall.cmd. GOG game files are never changed.
# Messages are Russian on a Russian/Ukrainian/Belarusian/Kazakh Windows, English otherwise.
$ErrorActionPreference = 'Stop'
$patchRoot = Split-Path $PSScriptRoot -Parent
$filesRoot = Join-Path $patchRoot 'files'
$manifestPath = Join-Path $PSScriptRoot 'files.sha256'
$version = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'version.txt') -TotalCount 1
$gogHashes = [ordered]@{
  'ResidentEvil3.exe' = 'F6AB367F757E6EE76695BC0D3E26957FB4E0A72A6E29F69CB3BC90EFD35123EC'
  'RE3_MERCE.exe'     = 'E409166C7D70C40A561412057E48F16979F37CBF20FA18977D9DB00DE0127A8B'
  'ddraw.dll'         = '0058B1EDDA05C709702D0CB5346EB866F34FF780DB52EAE27C5495A20C0CA1B9'
}
$iniValues = [ordered]@{ Width = '2560'; Height = '1920' }
$isRu = (Get-UICulture).TwoLetterISOLanguageName -in @('ru', 'uk', 'be', 'kk')

function L([string]$en, [string]$ru) { if ($isRu) { $ru } else { $en } }
function Say([string]$text, [string]$color = 'Gray') { Write-Host $text -ForegroundColor $color }
function Finish([int]$code) { if (-not $Quiet) { [void](Read-Host (L 'Press Enter to close this window' 'Нажмите Enter, чтобы закрыть окно')) }; exit $code }
function Fail([string]$text) { Say ''; Say ((L 'ERROR: ' 'ОШИБКА: ') + $text) Red; Say (L 'The mod was not installed (the GOG game files are unchanged).' 'Мод не установлен (файлы игры GOG не изменены).') Red; Finish 1 }
function IsGame([string]$path) { $path -and (Test-Path -LiteralPath (Join-Path $path 'ResidentEvil3.exe') -PathType Leaf) }
function Ask([string]$question) { if ($Quiet) { return $true }; $a = Read-Host "$question (Y/N)"; return $a -match '^\s*[YyДд]' }

try {
  Say ("Resident Evil 3DD Mod $version - " + (L 'install over the GOG version of Resident Evil 3' 'установка поверх GOG-версии Resident Evil 3')) Cyan
  if (-not $GamePath -and (IsGame (Split-Path $patchRoot -Parent))) { $GamePath = Split-Path $patchRoot -Parent }
  if (-not $GamePath) {
    foreach ($key in 'HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games\1266089300', 'HKLM:\SOFTWARE\GOG.com\Games\1266089300') {
      $p = (Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue).path
      if (IsGame $p) { $GamePath = $p; break }
    }
  }
  if (-not $GamePath -and -not $Quiet) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = L 'Resident Evil 3 (GOG) folder, the one with ResidentEvil3.exe' 'Папка Resident Evil 3 (GOG), в которой лежит ResidentEvil3.exe'
    if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { $GamePath = $dialog.SelectedPath }
  }
  if (-not (IsGame $GamePath)) { Fail (L 'the game folder with ResidentEvil3.exe was not found. Give it like this: Install.cmd "C:\path\to\Resident Evil 3"' 'не найдена папка игры с ResidentEvil3.exe. Укажите её так: Install.cmd "C:\путь\к\Resident Evil 3"') }
  $GamePath = (Resolve-Path -LiteralPath $GamePath).ProviderPath.TrimEnd('\')
  Say ((L 'Game folder: ' 'Папка игры: ') + $GamePath)

  foreach ($name in $gogHashes.Keys) {
    $p = Join-Path $GamePath $name
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { Fail (L "$name is missing from the game folder - the full GOG version of Resident Evil 3 is required" "в папке игры нет $name - нужна полная GOG-версия Resident Evil 3") }
    if ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -ne $gogHashes[$name]) { Fail (L "$name differs from the GOG version 1.0 hotfix4 - the mod works only with it" "$name отличается от GOG-версии 1.0 hotfix4 - мод работает только с ней") }
  }
  if (Test-Path -LiteralPath (Join-Path $GamePath 'HD-Remaster\installed-files.sha256') -PathType Leaf) {
    Fail (L 'an earlier private build (HD Remaster) is installed here - remove it first: HD-Remaster\Uninstall.cmd' 'здесь установлена прежняя частная сборка (HD Remaster) - сначала удалите её: HD-Remaster\Uninstall.cmd')
  }
  $running = @(Get-Process -Name ResidentEvil3, RE3_MERCE, RE3Launcher -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($GamePath, [StringComparison]::OrdinalIgnoreCase) })
  if ($running.Count) { Fail (L 'the game is running - close it and start the installation again' 'игра запущена - закройте её и запустите установку снова') }

  $entries = foreach ($line in [IO.File]::ReadAllLines($manifestPath)) {
    if (-not $line.Trim()) { continue }
    $f = $line.Split("`t")
    [pscustomobject]@{ Hash = $f[0].ToUpperInvariant(); Size = [int64]$f[1]; Path = $f[2] }
  }
  $need = [int64]0
  foreach ($e in $entries) { $d = Join-Path $GamePath $e.Path; if (-not (Test-Path -LiteralPath $d -PathType Leaf) -or (Get-Item -LiteralPath $d).Length -ne $e.Size) { $need += $e.Size } }
  $drive = New-Object System.IO.DriveInfo ([IO.Path]::GetPathRoot($GamePath))
  $needGb = [math]::Round(($need + 200MB) / 1GB, 1); $freeGb = [math]::Round($drive.AvailableFreeSpace / 1GB, 1)
  Say (L "Mod files: $($entries.Count), space needed: $needGb GB, free: $freeGb GB" "Файлов мода: $($entries.Count), нужно места: $needGb ГБ, свободно: $freeGb ГБ")
  if ($drive.AvailableFreeSpace -lt $need + 200MB) { Fail (L "not enough space on drive $($drive.Name): $needGb GB needed" "мало места на диске $($drive.Name): нужно $needGb ГБ") }
  if (-not (Ask (L 'Install the mod into this folder?' 'Установить мод в эту папку?'))) { Say (L 'Cancelled.' 'Отменено.'); Finish 0 }

  $modDir = Join-Path $GamePath '3DD-Mod'
  $writable = $true
  try { New-Item -ItemType Directory -Force -Path $modDir | Out-Null; [IO.File]::WriteAllText((Join-Path $modDir 'write-test.tmp'), 'ok'); Remove-Item -LiteralPath (Join-Path $modDir 'write-test.tmp') }
  catch { $writable = $false; $why = $_.Exception.Message }
  if (-not $writable) {
    $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if ($admin -or $Quiet) { Fail (L "no write access to $GamePath ($why)" "нет права записи в $GamePath ($why)") }
    Say (L 'Administrator rights are needed - restarting the installation...' 'Нужны права администратора - перезапуск установки...') Yellow
    Start-Process powershell.exe -Verb RunAs -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-GamePath', "`"$GamePath`"")
    exit 0
  }
  $iniPath = Join-Path $GamePath 'bio3.ini'
  if (-not (Test-Path -LiteralPath $iniPath -PathType Leaf)) { Fail (L 'bio3.ini is missing from the game folder - start the game once without the mod' 'в папке игры нет bio3.ini - запустите игру один раз без мода') }
  $iniBackup = Join-Path $modDir 'bio3.ini.original'
  if (-not (Test-Path -LiteralPath $iniBackup)) { Copy-Item -LiteralPath $iniPath -Destination $iniBackup }
  $ownDll = ($entries | Where-Object { $_.Path -eq 'd3d9.dll' }).Hash
  $gameDll = Join-Path $GamePath 'd3d9.dll'
  $dllBackup = Join-Path $modDir 'd3d9.dll.original'
  if ((Test-Path -LiteralPath $gameDll) -and -not (Test-Path -LiteralPath (Join-Path $modDir 'installed-files.sha256')) -and -not (Test-Path -LiteralPath $dllBackup) -and
      (Get-FileHash -LiteralPath $gameDll -Algorithm SHA256).Hash -ne $ownDll) { Copy-Item -LiteralPath $gameDll -Destination $dllBackup; Say (L 'A foreign d3d9.dll was saved to 3DD-Mod\d3d9.dll.original' 'Чужой d3d9.dll сохранён в 3DD-Mod\d3d9.dll.original') Yellow }

  Say (L 'Copying files...' 'Копирование файлов...')
  & robocopy.exe $filesRoot $GamePath /E /R:2 /W:1 /MT:8 /NFL /NDL /NJH /NJS /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { Fail (L "copying failed (robocopy, code $LASTEXITCODE)" "копирование не удалось (robocopy, код $LASTEXITCODE)") }

  Say (L 'Checking files...' 'Проверка файлов...')
  $activity = L 'Checking the mod files' 'Проверка файлов мода'
  $i = 0; $fixed = 0
  foreach ($e in $entries) {
    $i++; if ($i % 250 -eq 0) { Write-Progress -Activity $activity -Status "$i / $($entries.Count)" -PercentComplete (100 * $i / $entries.Count) }
    $d = Join-Path $GamePath $e.Path
    if (-not (Test-Path -LiteralPath $d -PathType Leaf) -or (Get-FileHash -LiteralPath $d -Algorithm SHA256).Hash -ne $e.Hash) {
      New-Item -ItemType Directory -Force -Path (Split-Path $d -Parent) | Out-Null
      Copy-Item -LiteralPath (Join-Path $filesRoot $e.Path) -Destination $d -Force; $fixed++
      if ((Get-FileHash -LiteralPath $d -Algorithm SHA256).Hash -ne $e.Hash) { Fail (L "$($e.Path) differs from the patch after copying (damaged download or disk)" "файл $($e.Path) не совпадает с патчем после копирования (повреждён архив или диск)") }
    }
  }
  Write-Progress -Activity $activity -Completed
  if ($fixed) { Say (L "Files copied again: $fixed" "Докопировано файлов: $fixed") }

  $ini = [IO.File]::ReadAllText($iniPath, [Text.Encoding]::Default)
  foreach ($k in $iniValues.Keys) {
    $pattern = "(?m)^$k=[^\r\n]*"
    if ($ini -notmatch $pattern) { Fail (L "bio3.ini has no $k= line" "в bio3.ini нет строки $k=") }
    $ini = [regex]::Replace($ini, $pattern, ("$k=" + $iniValues[$k]).Replace('$', '$$'))
  }
  [IO.File]::WriteAllText($iniPath, $ini, [Text.Encoding]::Default)

  Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $modDir 'installed-files.sha256') -Force
  foreach ($name in 'Uninstall-3DD.ps1', 'Uninstall.cmd', 'version.txt') { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $modDir $name) -Force }
  foreach ($name in 'README.txt', 'ПРОЧТИ.txt') { Copy-Item -LiteralPath (Join-Path $patchRoot $name) -Destination (Join-Path $modDir $name) -Force }

  Say ''
  Say (L "Done: Resident Evil 3DD Mod $version is installed ($($entries.Count) files checked)." "Готово: Resident Evil 3DD Mod $version установлен ($($entries.Count) файлов проверено).") Green
  Say (L 'Start the game as usual: GOG Galaxy, RE3Launcher.exe, ResidentEvil3.exe or RE3_MERCE.exe (Mercenaries). In the game F8 switches original/mod, F7 60/30 fps.' 'Запуск - как обычно: GOG Galaxy, RE3Launcher.exe, ResidentEvil3.exe или RE3_MERCE.exe (Mercenaries). F8 в игре - оригинал/мод, F7 - 60/30 кадров.')
  Say ((L 'To remove the mod: ' 'Удаление мода: ') + "$modDir\Uninstall.cmd")
  Finish 0
} catch { Fail $_.Exception.Message }
