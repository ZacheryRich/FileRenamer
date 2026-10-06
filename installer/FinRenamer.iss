; Inno Setup script for FinRenamer.
;
; Normally built by the CMake "installer" target, which passes the version and
; the dist folder. To build by hand instead:
;   1. cmake --install build --config Release --component FinRenamer --prefix dist
;   2. Open this file in Inno Setup and click Build > Compile
; The installer is written to installer\Output\.

#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#ifndef DistDir
  #define DistDir "..\dist"
#endif

#define MyAppName      "FinRenamer"
#define MyAppPublisher "FinRenamer"
#define MyAppExeName   "FinRenamer.exe"

[Setup]
; AppId identifies this program to Windows across versions, so a newer
; installer upgrades the old install instead of adding a second copy.
; Never change it. (The doubled {{ is how Inno writes a literal brace.)
AppId={{29D00348-3DA7-40E3-9530-B3D7DCEB76E2}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}

; Installs for the current user by default (no admin rights needed), into
; %LOCALAPPDATA%\Programs\FinRenamer. Someone with admin rights can choose
; "all users" in the first dialog, which installs to Program Files instead.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\{#MyAppName}
DisableProgramGroupPage=yes

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

OutputBaseFilename=FinRenamer-Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\bin\{#MyAppExeName}
UninstallDisplayName={#MyAppName}

; Close a running copy before replacing its files.
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Everything CMake put in the dist folder: bin\FinRenamer.exe, Qt DLLs and
; plugins, qt.conf, and the C++ runtime DLLs.
Source: "{#DistDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\bin\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\bin\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

; Note: the database and settings in %APPDATA%\FinRenamer are NOT touched by
; installing, upgrading or uninstalling -- cases and rename history survive.
