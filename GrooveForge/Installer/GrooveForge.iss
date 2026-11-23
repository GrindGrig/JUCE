; Groove Forge - Windows installer (Inno Setup 6)
;
; Normally run through build-installer.ps1, which composes License.txt and
; passes the defines below. To run it by hand:
;   iscc /DAppVersion=0.2.0 /DArtefactsDir=C:\path\to\build\GrooveForge_artefacts\Release GrooveForge.iss

#ifndef AppVersion
  #define AppVersion "0.2.0"
#endif
#ifndef ArtefactsDir
  #define ArtefactsDir "..\..\build\GrooveForge_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "Output"
#endif
#ifndef SourceUrl
  #define SourceUrl ""
#endif

#define AppName "Groove Forge"
#define AppExe  "Groove Forge.exe"

[Setup]
AppId={{3C08149C-2F74-4C96-AC2D-D170D0E63079}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Groove Forge
#if SourceUrl != ""
AppPublisherURL={#SourceUrl}
AppSupportURL={#SourceUrl}
#endif
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=License.txt
OutputDir={#OutputDir}
OutputBaseFilename=GrooveForge-{#AppVersion}-Windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; VST3 plug-ins go to the system-wide Common Files folder, which needs admin rights
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\{#AppExe}
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} installer

[Types]
Name: "full";   Description: "VST3 plug-in and standalone app"
Name: "plugin"; Description: "VST3 plug-in only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3";       Description: "VST3 plug-in (C:\Program Files\Common Files\VST3)"; Types: full plugin custom
Name: "standalone"; Description: "Standalone app";                                     Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Components: standalone; Flags: unchecked

[InstallDelete]
; remove an older copy of the bundle first so no stale files are left inside it
Type: filesandordirs; Name: "{commoncf64}\VST3\{#AppName}.vst3"; Components: vst3

[Files]
Source: "{#ArtefactsDir}\VST3\{#AppName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#AppName}.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#ArtefactsDir}\Standalone\{#AppExe}";   DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "License.txt";                            DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md";                           DestDir: "{app}"; DestName: "README.md"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; Components: standalone
Name: "{autodesktop}\{#AppName}";  Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Start {#AppName}"; Components: standalone; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\{#AppName}.vst3"

[Messages]
FinishedLabel=Setup has installed [name] on your computer.%n%nIn your DAW, rescan VST3 plug-ins and load Groove Forge as an instrument on a new track.
