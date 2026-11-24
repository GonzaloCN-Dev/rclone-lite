; Instalador de Rclone Lite (Inno Setup 6).
; Compilar con: installer\build-installer.ps1

#define AppName "Rclone Lite"
#define AppVersion "1.0.0"
#define AppPublisher "Gonzalo Cruz Nuñez"
#define AppExe "RcloneLite.exe"

[Setup]
AppId={{B7C2D5E1-4F0A-4E9B-9A1C-52C1E7A3D811}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppCopyright=© 2026 {#AppPublisher}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription=Instalador de {#AppName}
DefaultDirName={autopf}\Rclone Lite
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
SetupIconFile=..\assets\icon.ico
OutputDir=..\dist
OutputBaseFilename=RcloneLite-Setup-{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes
RestartApplications=no
DisableProgramGroupPage=yes
LicenseFile=..\installer\LICENSES.txt

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el escritorio"; GroupDescription: "Accesos directos:"; Flags: unchecked
Name: "autostart"; Description: "Iniciar Rclone Lite con Windows (minimizado en la bandeja)"; GroupDescription: "Opciones:"; Flags: unchecked

[Files]
Source: "payload\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\slint_cpp.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\rclone.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "LICENSES.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "RcloneLite"; ValueData: """{app}\{#AppExe}"" --minimized"; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#AppExe}"; Description: "Abrir {#AppName}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM {#AppExe}"; Flags: runhidden; RunOnceId: "KillApp"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and (not UninstallSilent()) then
    if MsgBox('¿Quieres borrar también tus trabajos, montajes y ajustes de Rclone Lite?' + #13#10 +
              '(Tus remotos de rclone no se tocan.)', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DelTree(ExpandConstant('{userappdata}\RcloneLite'), True, True, True);
end;
