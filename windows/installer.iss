; The installer owns only the launcher directory, never user game/build/save data.
#ifdef OfficialRelease
#define AppVersion "0.2.0"
#define AppChannel "Stable - DirectX 12"
#else
#define AppVersion "0.1.10"
#define AppChannel "experimental"
#endif

[Setup]
AppId=DisgaeaD2RecompLauncherWindows
AppName=Disgaea D2 Windows Launcher
AppVersion={#AppVersion}
AppVerName=Disgaea D2 Windows Launcher {#AppVersion} ({#AppChannel})
AppPublisher=LUUKASR1
AppPublisherURL=https://github.com/LUUKASR1/disgaea-d2-recomp-windows
AppSupportURL=https://github.com/LUUKASR1/disgaea-d2-recomp-windows/issues
DefaultDirName={localappdata}\Programs\DisgaeaD2Launcher
DefaultGroupName=Disgaea D2 Recomp
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0
DisableProgramGroupPage=yes
WizardStyle=modern
SetupIconFile=assets\launcher.ico
CloseApplications=no
RestartApplications=no
#ifdef OfficialRelease
InfoBeforeFile=official-installer-info.txt
#else
InfoBeforeFile=installer-info.txt
#endif
OutputDir=..\dist-installer
OutputBaseFilename=DisgaeaD2Launcher-Setup-Windows-x64
Compression=lzma2/fast
SolidCompression=yes
UninstallDisplayIcon={app}\DisgaeaD2Launcher.exe
VersionInfoVersion={#AppVersion}.0
VersionInfoDescription=Disgaea D2 Windows Launcher - {#AppChannel}

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Criar atalho na area de trabalho"; GroupDescription: "Atalhos:"; Flags: unchecked

[Files]
#ifdef OfficialRelease
Source: "..\dist\DisgaeaD2Launcher.exe"; DestDir: "{app}"; Flags: ignoreversion
#else
Source: "..\dist\DisgaeaD2Launcher\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
#endif

[Icons]
Name: "{userprograms}\Disgaea D2 Recomp\DisgaeaD2Launcher"; Filename: "{app}\DisgaeaD2Launcher.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\DisgaeaD2Launcher"; Filename: "{app}\DisgaeaD2Launcher.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\DisgaeaD2Launcher.exe"; Parameters: "--runtime-setup"; StatusMsg: "Verificando bibliotecas de execucao do Windows..."; Flags: waituntilterminated
Filename: "{app}\DisgaeaD2Launcher.exe"; Description: "Abrir DisgaeaD2Launcher"; Flags: nowait postinstall skipifsilent

; No [UninstallDelete] or [InstallDelete] sections: LocalAppData game/build/save
; folders and arbitrary extra files in the launcher directory are preserved.
