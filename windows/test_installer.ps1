param([switch]$OfficialRelease)
$ErrorActionPreference = 'Stop'
$installer = (Resolve-Path 'dist-installer/DisgaeaD2Launcher-Setup-Windows-x64.exe').Path
$installDirectory = Join-Path $env:RUNNER_TEMP 'D2 Launcher installed test'
$profileName = if ($OfficialRelease) { 'DisgaeaD2Recomp-Graphics-Preview' } else { 'DisgaeaD2Recomp' }
$dataDirectory = Join-Path $env:LOCALAPPDATA $profileName
$fixture = Join-Path $dataDirectory $(if ($OfficialRelease) { 'hdd0/home/00000001/savedata/CI-PRESERVE-FIXTURE/probe.txt' } else { 'auxiliar-rpcs3/dev_hdd0/home/00000001/savedata/CI-PRESERVE-FIXTURE/probe.txt' })
$config = Join-Path $dataDirectory 'build-atual.json'
$extra = Join-Path $installDirectory 'user-extra.txt'
New-Item -ItemType Directory -Path (Split-Path $fixture), $installDirectory -Force | Out-Null
[IO.File]::WriteAllText($fixture, 'CI-only save preservation fixture')
[IO.File]::WriteAllText($config, '{"ci_fixture":"preserve configuration"}')
[IO.File]::WriteAllText($extra, 'User-owned file, not installed by Setup')
$saveHash = (Get-FileHash $fixture).Hash
$configHash = (Get-FileHash $config).Hash

function Invoke-InstallerProcess($file, $arguments, $label) {
    $process = Start-Process -FilePath $file -ArgumentList $arguments -PassThru
    if (!$process.WaitForExit(120000)) {
        Stop-Process -Id $process.Id -Force
        throw "$label timed out"
    }
    $process.Refresh()
    if ($process.ExitCode -ne 0) { throw "$label failed: exit $($process.ExitCode)" }
}

function Assert-PreservedData {
    if ((Get-FileHash $fixture).Hash -ne $saveHash) { throw 'Save fixture changed' }
    if ((Get-FileHash $config).Hash -ne $configHash) { throw 'Build configuration changed' }
    if (!(Test-Path $extra)) { throw 'User-owned extra file deleted' }
}

$arguments = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /LANG=brazilianportuguese /TASKS=desktopicon /DIR=`"$installDirectory`""
Invoke-InstallerProcess $installer $arguments 'Install'
$launcher = Join-Path $installDirectory 'DisgaeaD2Launcher.exe'
if ((Get-FileHash $launcher).Hash -ne (Get-FileHash $(if ($OfficialRelease) { 'dist/DisgaeaD2Launcher.exe' } else { 'dist/DisgaeaD2Launcher/DisgaeaD2Launcher.exe' })).Hash) {
    throw 'Installed executable differs from tested package'
}
if (!$OfficialRelease -and !(Test-Path (Join-Path $installDirectory '_internal/vendor/cmake/bin/cmake.exe'))) { throw 'CMake payload missing' }
if (!$OfficialRelease -and !(Test-Path (Join-Path $installDirectory '_internal/windows/perfil.json'))) { throw 'Profile missing' }
$startShortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'Disgaea D2 Recomp/DisgaeaD2Launcher.lnk'
$desktopShortcut = Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'DisgaeaD2Launcher.lnk'
if (!(Test-Path $startShortcut) -or !(Test-Path $desktopShortcut)) { throw 'Installer shortcuts missing' }
Assert-PreservedData
Invoke-InstallerProcess $launcher '--self-test' 'Installed interpreter self-test'
Invoke-InstallerProcess $installer $arguments 'Reinstall'
Assert-PreservedData
Invoke-InstallerProcess (Join-Path $installDirectory 'unins000.exe') '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' 'Uninstall'
Assert-PreservedData
if (Test-Path $launcher) { throw 'Launcher was not uninstalled' }
if (Test-Path (Join-Path $installDirectory '_internal/vendor/cmake/bin/cmake.exe')) { throw 'Installed dependency was not removed' }
if ((Test-Path $startShortcut) -or (Test-Path $desktopShortcut)) { throw 'Installer shortcuts remained' }
Write-Output 'PASS: install, installed interpreter, reinstall, shortcuts, uninstall and preservation of save/config/user files.'
