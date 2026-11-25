; Instalador de Rclone Lite (Inno Setup 6).
; Compilar con: installer\build-installer.ps1

#define AppName "Rclone Lite"
#define AppVersion "1.3.2"
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
ArchiveExtraction=full

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el escritorio"; GroupDescription: "Accesos directos:"; Flags: unchecked
Name: "autostart"; Description: "Iniciar Rclone Lite con Windows (minimizado en la bandeja)"; GroupDescription: "Opciones:"; Flags: unchecked
Name: "getrclone"; Description: "Descargar e instalar rclone (no se ha encontrado en este equipo)"; GroupDescription: "Componentes que faltan:"; Check: NeedRclone
Name: "getwinfsp"; Description: "Descargar e instalar WinFsp (solo hace falta para montar unidades; pide permisos de administrador)"; GroupDescription: "Componentes que faltan:"; Flags: unchecked; Check: NeedWinFsp

[Files]
Source: "payload\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\slint_cpp.dll"; DestDir: "{app}"; Flags: ignoreversion
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
Filename: "{app}\{#AppExe}"; Flags: nowait runasoriginaluser; Check: RelaunchRequested

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM {#AppExe}"; Flags: runhidden; RunOnceId: "KillApp"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
function RelaunchRequested: Boolean;
begin
  Result := ExpandConstant('{param:RELAUNCH|0}') = '1';
end;

function RcloneInstalled: Boolean;
begin
  Result := FileSearch('rclone.exe', GetEnv('PATH')) <> '';
  if not Result then
    Result := FileExists(AddBackslash(WizardDirValue) + 'rclone.exe');
end;
function WinFspInstalled: Boolean;
begin
  Result := FileExists(ExpandConstant('{commonpf32}\WinFsp\bin\winfsp-x64.dll')) or
            FileExists(ExpandConstant('{commonpf64}\WinFsp\bin\winfsp-x64.dll'));
end;

function NeedRclone: Boolean;
begin
  Result := not RcloneInstalled;
end;

function NeedWinFsp: Boolean;
begin
  Result := not WinFspInstalled;
end;

function OnDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  if ProgressMax > 0 then
    WizardForm.StatusLabel.Caption := Format('Descargando %s: %d de %d MB', [FileName, Progress div 1048576, ProgressMax div 1048576])
  else
    WizardForm.StatusLabel.Caption := Format('Descargando %s: %d MB', [FileName, Progress div 1048576]);
  Result := True;
end;

function ReadFirstLine(const FileName: String): String;
var
  Lines: TArrayOfString;
begin
  Result := '';
  if LoadStringsFromFile(FileName, Lines) and (GetArrayLength(Lines) > 0) then
    Result := Trim(Lines[0]);
end;

function InstallRclone: String;
var
  Version, ZipName, Sha, Line, TmpDir: String;
  Lines: TArrayOfString;
  I: Integer;
begin
  Result := '';
  Log('Descargando la ultima version de rclone');
  DownloadTemporaryFile('https://downloads.rclone.org/version.txt', 'rclone-version.txt', '', @OnDownloadProgress);
  Version := ReadFirstLine(ExpandConstant('{tmp}\rclone-version.txt'));
  if Pos('rclone v', Version) <> 1 then
    RaiseException('No se pudo leer la ultima version de rclone.');
  Version := Copy(Version, 8, Length(Version));
  for I := 1 to Length(Version) do
    if not (((Version[I] >= '0') and (Version[I] <= '9')) or (Version[I] = '.') or (Version[I] = 'v')) then
      RaiseException('La version de rclone no es valida.');

  ZipName := 'rclone-' + Version + '-windows-amd64.zip';
  DownloadTemporaryFile('https://downloads.rclone.org/' + Version + '/SHA256SUMS', 'rclone-sums.txt', '', @OnDownloadProgress);
  Sha := '';
  if LoadStringsFromFile(ExpandConstant('{tmp}\rclone-sums.txt'), Lines) then
    for I := 0 to GetArrayLength(Lines) - 1 do
    begin
      Line := Trim(Lines[I]);
      if (Length(Line) > 66) and (Copy(Line, Length(Line) - Length(ZipName) + 1, Length(ZipName)) = ZipName) then
      begin
        Sha := Lowercase(Copy(Line, 1, 64));
        Break;
      end;
    end;
  if Length(Sha) <> 64 then
    RaiseException('No se encontro la suma SHA-256 de rclone.');

  DownloadTemporaryFile('https://downloads.rclone.org/' + Version + '/' + ZipName, ZipName, Sha, @OnDownloadProgress);
  WizardForm.StatusLabel.Caption := 'Instalando rclone...';
  TmpDir := ExpandConstant('{tmp}\rclone-unzip');
  ForceDirectories(TmpDir);
  ExtractArchive(ExpandConstant('{tmp}\' + ZipName), TmpDir, '', True, nil);
  if not CopyFile(TmpDir + '\rclone-' + Version + '-windows-amd64\rclone.exe', ExpandConstant('{app}\rclone.exe'), False) then
    RaiseException('No se pudo copiar rclone.exe a la carpeta de la aplicacion.');
  Log('rclone ' + Version + ' instalado');
end;

function JsonStringAfter(const Json, Key: String): String;
var
  P: Integer;
begin
  Result := '';
  P := Pos(Key, Json);
  if P = 0 then
    Exit;
  Result := Copy(Json, P + Length(Key), Length(Json));
  P := Pos('"', Result);
  if P = 0 then
    Result := ''
  else
    Result := Copy(Result, 1, P - 1);
end;

function InstallWinFsp: String;
var
  Json, Rest, Name, Url, Sha, MsiPath: String;
  Raw: AnsiString;
  P, ResultCode: Integer;
begin
  Result := '';
  Log('Descargando la ultima version de WinFsp');
  DownloadTemporaryFile('https://api.github.com/repos/winfsp/winfsp/releases/latest', 'winfsp-release.json', '', @OnDownloadProgress);
  if not LoadStringFromFile(ExpandConstant('{tmp}\winfsp-release.json'), Raw) then
    RaiseException('No se pudo leer la informacion de WinFsp.');
  Json := String(Raw);
  StringChangeEx(Json, '": "', '":"', True);

  Url := '';
  Sha := '';
  Rest := Json;
  P := Pos('"name":"winfsp-', Rest);
  while P > 0 do
  begin
    Rest := Copy(Rest, P, Length(Rest));
    Name := JsonStringAfter(Rest, '"name":"');
    if (Length(Name) > 4) and (Lowercase(Copy(Name, Length(Name) - 3, 4)) = '.msi') and (Pos('tests', Name) = 0) then
    begin
      Sha := Lowercase(JsonStringAfter(Rest, '"digest":"sha256:'));
      Url := JsonStringAfter(Rest, '"browser_download_url":"');
      Break;
    end;
    Rest := Copy(Rest, 10, Length(Rest));
    P := Pos('"name":"winfsp-', Rest);
  end;
  if (Pos('https://github.com/winfsp/winfsp/releases/download/', Url) <> 1) or (Length(Sha) <> 64) then
    RaiseException('No se pudo obtener una descarga verificable de WinFsp.');

  DownloadTemporaryFile(Url, 'winfsp.msi', Sha, @OnDownloadProgress);
  MsiPath := ExpandConstant('{tmp}\winfsp.msi');
  WizardForm.StatusLabel.Caption := 'Instalando WinFsp (acepta la ventana de permisos de administrador)...';
  if not ShellExec('runas', ExpandConstant('{sys}\msiexec.exe'), '/i "' + MsiPath + '" /qb /norestart', '', SW_SHOWNORMAL, ewWaitUntilTerminated, ResultCode) then
    RaiseException('No se pudo iniciar la instalacion de WinFsp (permiso de administrador denegado).');
  if (ResultCode <> 0) and (ResultCode <> 3010) then
    RaiseException('La instalacion de WinFsp termino con el codigo ' + IntToStr(ResultCode) + '.');
  Log('WinFsp instalado');
end;
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep <> ssPostInstall then
    Exit;
  if WizardIsTaskSelected('getrclone') then
    try
      InstallRclone;
    except
      SuppressibleMsgBox('No se pudo instalar rclone automaticamente:' + #13#10 + GetExceptionMessage + #13#10#13#10 +
        'Rclone Lite se ha instalado igualmente. Puedes instalar rclone mas tarde desde https://rclone.org/downloads/ o indicar su ruta en Ajustes.',
        mbError, MB_OK, IDOK);
    end;
  if WizardIsTaskSelected('getwinfsp') then
    try
      InstallWinFsp;
    except
      SuppressibleMsgBox('No se pudo instalar WinFsp automaticamente:' + #13#10 + GetExceptionMessage + #13#10#13#10 +
        'Rclone Lite se ha instalado igualmente. Puedes instalar WinFsp mas tarde desde https://winfsp.dev (solo hace falta para montar unidades).',
        mbError, MB_OK, IDOK);
    end;
end;
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and (not UninstallSilent()) then
    if MsgBox('¿Quieres borrar también tus trabajos, montajes y ajustes de Rclone Lite?' + #13#10 +
              '(Tus remotos de rclone no se tocan.)', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DelTree(ExpandConstant('{userappdata}\RcloneLite'), True, True, True);
end;
