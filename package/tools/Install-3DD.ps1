param([string]$GamePath, [switch]$Quiet)
# Resident Evil 3DD Mod: installs the mod over the GOG version (1.0 hotfix4) or the Steam version (app 4249120, English) of
# Resident Evil 3.
#   Install.cmd                     - finds the game (the folder above the patch, GOG registry, Steam), asks, installs
#   Install.cmd "D:\Games\RE3"      - installs into the given folder (for Steam: the game folder or its english folder)
# Copies files\* into the game folder, checks every file against tools\files.sha256 and sets bio3.ini Width/Height to the
# render size of files\pilot.ini (render_width/render_height; 2560x1920 when absent, Lite 1440x1080). The game keeps its own
# movies, Movie= is not touched. The original bio3.ini (and a foreign d3d9.dll, if any) is kept in <game>\3DD-Mod, together
# with Uninstall.cmd. Game files are never changed.
# Steam (02.11.2026): the Steam folder holds the GOG English files in english\ (the exes are the same GOG builds wrapped by
# Steam's protection), so the mod goes into <Steam game folder>\english; the other languages are other exe builds.
# Messages are Russian on a Russian/Ukrainian/Belarusian/Kazakh Windows, English otherwise.
$ErrorActionPreference = 'Stop'
$patchRoot = Split-Path $PSScriptRoot -Parent
$filesRoot = Join-Path $patchRoot 'files'
$manifestPath = Join-Path $PSScriptRoot 'files.sha256'
$version = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'version.txt') -TotalCount 1
$wrapperHash = '0058B1EDDA05C709702D0CB5346EB866F34FF780DB52EAE27C5495A20C0CA1B9'   # GOG ddraw.dll, the same in the Steam version
$builds = [ordered]@{
  'GOG'   = [ordered]@{ 'ResidentEvil3.exe' = 'F6AB367F757E6EE76695BC0D3E26957FB4E0A72A6E29F69CB3BC90EFD35123EC'; 'RE3_MERCE.exe' = 'E409166C7D70C40A561412057E48F16979F37CBF20FA18977D9DB00DE0127A8B' }
  'Steam' = [ordered]@{ 'ResidentEvil3.exe' = 'CA2E08CD59BF0D4729CFB1D969E1DCD791C3F65F865D9731CB50E2B98C81144F'; 'RE3_MERCE.exe' = '6A43DCB1BB045B173A6D52AB3E1DE4895CDF6B1246F23EEE73EA874951F16EDA' }
}
$steamApp = '4249120'
$isRu = (Get-UICulture).TwoLetterISOLanguageName -in @('ru', 'uk', 'be', 'kk')

function L([string]$en, [string]$ru) { if ($isRu) { $ru } else { $en } }
function Say([string]$text, [string]$color = 'Gray') { Write-Host $text -ForegroundColor $color }
function Finish([int]$code) { if (-not $Quiet) { [void](Read-Host (L 'Press Enter to close this window' 'Нажмите Enter, чтобы закрыть окно')) }; exit $code }
function Fail([string]$text) { Say ''; Say ((L 'ERROR: ' 'ОШИБКА: ') + $text) Red; Say (L 'The mod was not installed (the game files are unchanged).' 'Мод не установлен (файлы игры не изменены).') Red; Finish 1 }
function IsGame([string]$path) { $path -and (Test-Path -LiteralPath (Join-Path $path 'ResidentEvil3.exe') -PathType Leaf) }
function Ask([string]$question) { if ($Quiet) { return $true }; $a = Read-Host "$question (Y/N)"; return $a -match '^\s*[YyДд]' }
# A Steam game folder (4249120_Launcher.exe next to english\) -> its english folder
function GameFolder([string]$path) {
  if (-not $path) { return $null }
  if (-not (IsGame $path) -and (IsGame (Join-Path $path 'english'))) { return (Join-Path $path 'english') }
  return $path
}
function SteamFolders {
  $out = @()
  foreach ($key in "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App $steamApp", "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App $steamApp") {
    $p = (Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue).InstallLocation; if ($p) { $out += $p }
  }
  $steam = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
  if ($steam) {
    $libs = @($steam.Replace('/', '\'))
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $vdf) { foreach ($m in [regex]::Matches([IO.File]::ReadAllText($vdf), '"path"\s+"([^"]+)"')) { $libs += $m.Groups[1].Value.Replace('\\', '\') } }
    foreach ($lib in $libs) {
      $acf = Join-Path $lib "steamapps\appmanifest_$steamApp.acf"
      if (Test-Path -LiteralPath $acf) { $m = [regex]::Match([IO.File]::ReadAllText($acf), '"installdir"\s+"([^"]+)"'); if ($m.Success) { $out += (Join-Path $lib ('steamapps\common\' + $m.Groups[1].Value)) } }
    }
  }
  return $out
}

try {
  Say ("Resident Evil 3DD Mod $version - " + (L 'install over the GOG or Steam version of Resident Evil 3' 'установка поверх GOG- или Steam-версии Resident Evil 3')) Cyan
  $GamePath = GameFolder $GamePath
  if (-not $GamePath -and (IsGame (GameFolder (Split-Path $patchRoot -Parent)))) { $GamePath = GameFolder (Split-Path $patchRoot -Parent) }   # the patch unpacked inside the game folder
  if (-not $GamePath) {
    $found = New-Object System.Collections.Generic.List[string]
    function Add([string]$p, [string]$what) { $p = GameFolder $p; if ((IsGame $p) -and -not ($found | Where-Object { $_.Split('|')[1] -eq (Resolve-Path -LiteralPath $p).ProviderPath.TrimEnd('\') })) { $found.Add($what + '|' + (Resolve-Path -LiteralPath $p).ProviderPath.TrimEnd('\')) } }
    foreach ($key in 'HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games\1266089300', 'HKLM:\SOFTWARE\GOG.com\Games\1266089300') { Add (Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue).path 'GOG' }
    foreach ($p in SteamFolders) { Add $p 'Steam' }
    if ($found.Count -eq 1 -or ($found.Count -gt 1 -and $Quiet)) { $GamePath = $found[0].Split('|')[1] }
    elseif ($found.Count -gt 1) {
      Say (L 'Resident Evil 3 was found in several places:' 'Resident Evil 3 найдена в нескольких местах:')
      for ($i = 0; $i -lt $found.Count; $i++) { $w, $p = $found[$i].Split('|'); Say ("  $($i + 1)) $w`: $p") }
      $n = Read-Host (L "Install into which one (1-$($found.Count))?" "В какую установить (1-$($found.Count))?")
      if ($n -notmatch '^\s*\d+\s*$' -or [int]$n -lt 1 -or [int]$n -gt $found.Count) { Say (L 'Cancelled.' 'Отменено.'); Finish 0 }
      $GamePath = $found[[int]$n - 1].Split('|')[1]
    }
  }
  if (-not $GamePath -and -not $Quiet) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = L 'Resident Evil 3 folder: GOG - the one with ResidentEvil3.exe, Steam - 4249120_Biohazard3' 'Папка Resident Evil 3: GOG - та, в которой лежит ResidentEvil3.exe, Steam - 4249120_Biohazard3'
    if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { $GamePath = GameFolder $dialog.SelectedPath }
  }
  if (-not (IsGame $GamePath)) { Fail (L 'the game folder with ResidentEvil3.exe was not found. Give it like this: Install.cmd "C:\path\to\Resident Evil 3"' 'не найдена папка игры с ResidentEvil3.exe. Укажите её так: Install.cmd "C:\путь\к\Resident Evil 3"') }
  $GamePath = (Resolve-Path -LiteralPath $GamePath).ProviderPath.TrimEnd('\')
  Say ((L 'Game folder: ' 'Папка игры: ') + $GamePath)

  $exeHash = @{}
  foreach ($name in 'ResidentEvil3.exe', 'RE3_MERCE.exe', 'ddraw.dll') {
    $p = Join-Path $GamePath $name
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { Fail (L "$name is missing from the game folder - the full GOG or Steam version of Resident Evil 3 is required" "в папке игры нет $name - нужна полная GOG- или Steam-версия Resident Evil 3") }
    $exeHash[$name] = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
  }
  $edition = $null
  foreach ($b in $builds.Keys) { if ($exeHash['ResidentEvil3.exe'] -eq $builds[$b]['ResidentEvil3.exe'] -and $exeHash['RE3_MERCE.exe'] -eq $builds[$b]['RE3_MERCE.exe']) { $edition = $b } }
  $steamRoot = Split-Path $GamePath -Parent
  $steamFolder = Test-Path -LiteralPath (Join-Path $steamRoot "${steamApp}_Launcher.exe") -PathType Leaf
  if (-not $edition -or $exeHash['ddraw.dll'] -ne $wrapperHash) {
    if ($steamFolder -and (Split-Path $GamePath -Leaf) -ne 'english') { Fail (L "this is the $(Split-Path $GamePath -Leaf) folder of the Steam version - the mod works only with the English one: $steamRoot\english" "это папка $(Split-Path $GamePath -Leaf) Steam-версии - мод работает только с английской: $steamRoot\english") }
    Fail (L 'ResidentEvil3.exe, RE3_MERCE.exe or ddraw.dll differ from the GOG version 1.0 hotfix4 and from the Steam version (English) - the mod works only with them' 'ResidentEvil3.exe, RE3_MERCE.exe или ddraw.dll отличаются от GOG-версии 1.0 hotfix4 и от Steam-версии (английской) - мод работает только с ними')
  }
  Say ((L 'Version: ' 'Версия игры: ') + $(if ($edition -eq 'Steam') { 'Steam (english)' } else { 'GOG 1.0 hotfix4' }))
  if ($edition -eq 'Steam') {
    $launcherIni = Join-Path $steamRoot "${steamApp}_Launcher.ini"
    $lang = if (Test-Path -LiteralPath $launcherIni) { ([regex]::Match([IO.File]::ReadAllText($launcherIni), '(?m)^LanguageSetting=(\S+)')).Groups[1].Value } else { '' }
    if ($lang -and $lang -ne 'english') { Say (L "Note: the Steam launcher of the game is set to '$lang'. The mod works only in the English version: choose English in the launcher (Language) before playing." "Внимание: в лаунчере Steam выбран язык '$lang'. Мод работает только в английской версии: перед игрой выберите в лаунчере English (Language).") Yellow }
  }
  if (Test-Path -LiteralPath (Join-Path $GamePath 'HD-Remaster\installed-files.sha256') -PathType Leaf) {
    Fail (L 'an earlier private build (HD Remaster) is installed here - remove it first: HD-Remaster\Uninstall.cmd' 'здесь установлена прежняя частная сборка (HD Remaster) - сначала удалите её: HD-Remaster\Uninstall.cmd')
  }
  $running = @(Get-Process -Name ResidentEvil3, RE3_MERCE, RE3Launcher, "${steamApp}_Launcher" -ErrorAction SilentlyContinue | Where-Object { $_.Path -and ($_.Path.StartsWith($GamePath, [StringComparison]::OrdinalIgnoreCase) -or ($steamFolder -and $_.Path.StartsWith($steamRoot, [StringComparison]::OrdinalIgnoreCase))) })
  if ($running.Count) { Fail (L 'the game is running - close it and start the installation again' 'игра запущена - закройте её и запустите установку снова') }

  # bio3.ini Width/Height = the render size of the patch's pilot.ini (the DLL patches the game's 1024x768 mode to it)
  $pilot = [IO.File]::ReadAllText((Join-Path $filesRoot 'pilot.ini'))
  $rw = [regex]::Match($pilot, '(?m)^render_width=(\d+)'); $rh = [regex]::Match($pilot, '(?m)^render_height=(\d+)')
  $iniValues = [ordered]@{ Width = $(if ($rw.Success) { $rw.Groups[1].Value } else { '2560' }); Height = $(if ($rh.Success) { $rh.Groups[1].Value } else { '1920' }) }

  $entries = foreach ($line in [IO.File]::ReadAllLines($manifestPath)) {
    if (-not $line.Trim()) { continue }
    $f = $line.Split("`t")
    [pscustomobject]@{ Hash = $f[0].ToUpperInvariant(); Size = [int64]$f[1]; Path = $f[2] }
  }
  $need = [int64]0
  foreach ($e in $entries) { $d = Join-Path $GamePath $e.Path; if (-not (Test-Path -LiteralPath $d -PathType Leaf) -or (Get-Item -LiteralPath $d).Length -ne $e.Size) { $need += $e.Size } }
  # An update or a switch between the mod and its Lite edition: files the installed version listed and this one no longer has
  # (1.19: pack\*\background.png and atlas.png became .r3tx) are removed before copying. Only paths of the earlier manifest
  # inside the game folder.
  $modDir = Join-Path $GamePath '3DD-Mod'
  $oldManifest = Join-Path $modDir 'installed-files.sha256'
  $stale = @(); $staleBytes = [int64]0
  if (Test-Path -LiteralPath $oldManifest -PathType Leaf) {
    $keep = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($e in $entries) { [void]$keep.Add($e.Path) }
    foreach ($line in [IO.File]::ReadAllLines($oldManifest)) {
      $f = $line.Split("`t"); if ($f.Count -lt 3) { continue }; $rel = $f[2]
      if ($keep.Contains($rel) -or [IO.Path]::IsPathRooted($rel) -or $rel -match '(^|[\\/])\.\.([\\/]|$)') { continue }
      $p = Join-Path $GamePath $rel
      if (Test-Path -LiteralPath $p -PathType Leaf) { $stale += $p; $staleBytes += (Get-Item -LiteralPath $p).Length }
    }
  }
  $drive = New-Object System.IO.DriveInfo ([IO.Path]::GetPathRoot($GamePath))
  $needGb = [math]::Round(($need + 200MB) / 1GB, 1); $freeGb = [math]::Round(($drive.AvailableFreeSpace + $staleBytes) / 1GB, 1)
  Say (L "Mod files: $($entries.Count), space needed: $needGb GB, free: $freeGb GB" "Файлов мода: $($entries.Count), нужно места: $needGb ГБ, свободно: $freeGb ГБ")
  if ($stale.Count) { Say (L "Files of the installed version to remove: $($stale.Count)" "Файлов прежней версии к удалению: $($stale.Count)") }
  if ($drive.AvailableFreeSpace + $staleBytes -lt $need + 200MB) { Fail (L "not enough space on drive $($drive.Name): $needGb GB needed" "мало места на диске $($drive.Name): нужно $needGb ГБ") }
  if (-not (Ask (L 'Install the mod into this folder?' 'Установить мод в эту папку?'))) { Say (L 'Cancelled.' 'Отменено.'); Finish 0 }

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

  if ($stale.Count) {
    foreach ($p in $stale) { Remove-Item -LiteralPath $p -Force }
    Say (L "Files of the earlier version removed: $($stale.Count)" "Удалено файлов прежней версии: $($stale.Count)")
  }
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
  Say (L "Done: Resident Evil 3DD Mod $version is installed ($($entries.Count) files checked, render $($iniValues.Width)x$($iniValues.Height))." "Готово: Resident Evil 3DD Mod $version установлен ($($entries.Count) файлов проверено, рендер $($iniValues.Width)x$($iniValues.Height)).") Green
  if ($edition -eq 'Steam') {
    Say (L 'Start the game through Steam as usual: the game launcher, Story or Mercenaries (language English). In the game F8 switches original/mod, F7 60/30 fps.' 'Запуск - через Steam, как обычно: лаунчер игры, Story или Mercenaries (язык English). F8 в игре - оригинал/мод, F7 - 60/30 кадров.')
    Say (L 'After "Verify integrity of game files" in Steam start Install.cmd again (Steam puts back bio3.ini).' 'После «Проверить целостность файлов игры» в Steam запустите Install.cmd ещё раз (Steam вернёт bio3.ini).')
  } else {
    Say (L 'Start the game as usual: GOG Galaxy, RE3Launcher.exe, ResidentEvil3.exe or RE3_MERCE.exe (Mercenaries). In the game F8 switches original/mod, F7 60/30 fps.' 'Запуск - как обычно: GOG Galaxy, RE3Launcher.exe, ResidentEvil3.exe или RE3_MERCE.exe (Mercenaries). F8 в игре - оригинал/мод, F7 - 60/30 кадров.')
  }
  Say ((L 'To remove the mod: ' 'Удаление мода: ') + "$modDir\Uninstall.cmd")
  Finish 0
} catch { Fail $_.Exception.Message }
