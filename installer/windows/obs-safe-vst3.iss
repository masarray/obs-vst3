#ifndef MyAppVersion
#define MyAppVersion "0.0.0-dev"
#endif

#define MyAppName "OBS Safe VST3 Host"
#define MyAppPublisher "masarray"
#define MyAppURL "https://github.com/masarray/obs-vst3"

[Setup]
AppId={{9D803833-22DF-4BE6-AE6D-D14BD30C9850}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\OBS Safe VST3 Host
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\..\dist
OutputBaseFilename=OBS-Safe-VST3-Host-v{#MyAppVersion}-Setup-x64
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
UninstallDisplayName={#MyAppName}
UsePreviousAppDir=no

[Files]
Source: "payload\obs-safe-vst3.dll"; DestDir: "{code:GetPluginBinDir}"; Flags: ignoreversion
Source: "payload\obs-safe-vst3-host.exe"; DestDir: "{code:GetPluginBinDir}"; Flags: ignoreversion
Source: "payload\obs-safe-vst3-scanner.exe"; DestDir: "{code:GetPluginBinDir}"; Flags: ignoreversion
Source: "payload\en-US.ini"; DestDir: "{code:GetPluginLocaleDir}"; Flags: ignoreversion

[InstallDelete]
; Standard OBS can discover plug-ins from several historical locations. Clean
; only our exact plug-in files/folders so an older copy cannot win discovery
; order and silently shadow the package being installed now.
Type: filesandordirs; Name: "{commonappdata}\obs-studio\plugins\obs-safe-vst3"; Check: IsStandardMode
Type: filesandordirs; Name: "{userappdata}\obs-studio\plugins\obs-safe-vst3"; Check: IsStandardMode
Type: files; Name: "{autopf}\obs-studio\obs-plugins\64bit\obs-safe-vst3.dll"; Check: IsStandardMode
Type: files; Name: "{autopf}\obs-studio\obs-plugins\64bit\obs-safe-vst3-host.exe"; Check: IsStandardMode
Type: files; Name: "{autopf}\obs-studio\obs-plugins\64bit\obs-safe-vst3-scanner.exe"; Check: IsStandardMode
Type: filesandordirs; Name: "{autopf}\obs-studio\data\obs-plugins\obs-safe-vst3"; Check: IsStandardMode

[Code]
const
  SettingsRegKey = 'Software\masarray\OBS Safe VST3 Host';
  LastModeValue = 'LastInstallMode';
  LastPortableDirValue = 'LastPortableObsDir';

var
  InstallModePage: TInputOptionWizardPage;
  PortableDirPage: TInputDirWizardPage;
  PortableObsDirParam: String;
  CloseObsParam: String;

function AddSlash(const Path: String): String;
begin
  Result := Path;
  if (Result <> '') and (Result[Length(Result)] <> '\') then
    Result := Result + '\';
end;

function IsStandardMode: Boolean;
begin
  Result := InstallModePage.SelectedValueIndex = 0;
end;

function IsObsRootValid(const Root: String): Boolean;
begin
  Result := (Root <> '') and FileExists(AddSlash(Root) + 'bin\64bit\obs64.exe');
end;

function GetPluginBinDir(Param: String): String;
begin
  if IsStandardMode then
    Result := ExpandConstant('{commonappdata}\obs-studio\plugins\obs-safe-vst3\bin\64bit')
  else
    Result := AddSlash(PortableDirPage.Values[0]) + 'obs-plugins\64bit';
end;

function GetPluginLocaleDir(Param: String): String;
begin
  if IsStandardMode then
    Result := ExpandConstant('{commonappdata}\obs-studio\plugins\obs-safe-vst3\data\locale')
  else
    Result := AddSlash(PortableDirPage.Values[0]) + 'data\obs-plugins\obs-safe-vst3\locale';
end;

function IsObsRunning: Boolean;
var
  ResultCode: Integer;
  Params: String;
begin
  Params := '/C tasklist /FI "IMAGENAME eq obs64.exe" /NH | find /I "obs64.exe" >NUL 2>&1';
  Result := Exec(ExpandConstant('{cmd}'), Params, '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
end;

function WaitForObsExit(const HalfSecondAttempts: Integer): Boolean;
var
  I: Integer;
begin
  for I := 1 to HalfSecondAttempts do
  begin
    if not IsObsRunning then
    begin
      Result := True;
      Exit;
    end;
    Sleep(500);
  end;
  Result := not IsObsRunning;
end;

function TerminateObs(const Force: Boolean): Boolean;
var
  ResultCode: Integer;
  Params: String;
begin
  Params := '/C taskkill /T /IM obs64.exe';
  if Force then
    Params := Params + ' /F';

  Result := Exec(ExpandConstant('{cmd}'), Params, '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
end;

function RequestObsClose: Boolean;
var
  Answer: Integer;
  AllowForce: Boolean;
begin
  Result := True;
  if not IsObsRunning then
    Exit;

  AllowForce := False;

  if WizardSilent then
  begin
    if (CloseObsParam <> 'yes') and (CloseObsParam <> 'force') then
    begin
      Result := False;
      Exit;
    end;
    AllowForce := CloseObsParam = 'force';
  end
  else
  begin
    Answer := MsgBox(
      'OBS Studio is currently running.' + #13#10 + #13#10 +
      'Setup must close OBS before updating the plug-in files.' + #13#10 +
      'Save or stop any active recording/stream first.' + #13#10 + #13#10 +
      'Close OBS Studio automatically now?',
      mbConfirmation, MB_YESNO);
    if Answer <> IDYES then
    begin
      Result := False;
      Exit;
    end;
  end;

  { First request a normal process termination. Never force-close without a
    second explicit permission in the interactive installer. }
  TerminateObs(False);
  if WaitForObsExit(20) then
    Exit;

  if WizardSilent then
  begin
    if not AllowForce then
    begin
      Result := False;
      Exit;
    end;
  end
  else
  begin
    Answer := MsgBox(
      'OBS Studio did not close within 10 seconds.' + #13#10 + #13#10 +
      'Force-close OBS now?' + #13#10 +
      'Unsaved recording/stream state may be lost.',
      mbConfirmation, MB_YESNO);
    if Answer <> IDYES then
    begin
      Result := False;
      Exit;
    end;
  end;

  TerminateObs(True);
  Result := WaitForObsExit(10);
end;

procedure LoadRememberedInstallTarget;
var
  RememberedMode: String;
  RememberedPortableDir: String;
begin
  RememberedMode := '';
  RememberedPortableDir := '';

  RegQueryStringValue(HKEY_CURRENT_USER, SettingsRegKey,
    LastModeValue, RememberedMode);
  RegQueryStringValue(HKEY_CURRENT_USER, SettingsRegKey,
    LastPortableDirValue, RememberedPortableDir);

  if IsObsRootValid(RememberedPortableDir) then
  begin
    PortableDirPage.Values[0] := RememberedPortableDir;
    if CompareText(RememberedMode, 'portable') = 0 then
      InstallModePage.SelectedValueIndex := 1;
  end;
end;

procedure RememberInstallTarget;
begin
  if IsStandardMode then
  begin
    RegWriteStringValue(HKEY_CURRENT_USER, SettingsRegKey,
      LastModeValue, 'standard');
  end
  else
  begin
    RegWriteStringValue(HKEY_CURRENT_USER, SettingsRegKey,
      LastModeValue, 'portable');
    RegWriteStringValue(HKEY_CURRENT_USER, SettingsRegKey,
      LastPortableDirValue, PortableDirPage.Values[0]);
  end;
end;

procedure InitializeWizard;
var
  DefaultPortablePath: String;
begin
  InstallModePage := CreateInputOptionPage(wpWelcome,
    'Choose OBS installation type',
    'Where should OBS Safe VST3 Host be installed?',
    'Setup remembers the last successful installation mode and portable OBS folder. ' +
    'For a normal OBS Studio installation, use Standard. ' +
    'Choose Custom / Portable for a self-contained OBS folder.',
    True, False);

  InstallModePage.Add('Standard OBS Studio (recommended)');
  InstallModePage.Add('Custom / Portable OBS folder');
  InstallModePage.SelectedValueIndex := 0;

  PortableDirPage := CreateInputDirPage(InstallModePage.ID,
    'Select your OBS folder',
    'Choose the OBS Studio root folder',
    'Select the folder that contains bin\64bit\obs64.exe. ' +
    'Setup remembers this location for future updates.',
    False, '');
  PortableDirPage.Add('');

  DefaultPortablePath := ExpandConstant('{autopf}\obs-studio');
  if IsObsRootValid(DefaultPortablePath) then
    PortableDirPage.Values[0] := DefaultPortablePath
  else
    PortableDirPage.Values[0] := ExpandConstant('{sd}\OBS Studio');

  LoadRememberedInstallTarget;

  { Explicit command-line target always wins over remembered state. }
  PortableObsDirParam := ExpandConstant('{param:PortableObsDir|}');
  if PortableObsDirParam <> '' then
  begin
    InstallModePage.SelectedValueIndex := 1;
    PortableDirPage.Values[0] := PortableObsDirParam;
  end;

  { Silent automation can opt into closing OBS with /CloseObs=yes, or permit
    a force-close fallback with /CloseObs=force. Interactive installs always
    ask the user before either action. }
  CloseObsParam := Lowercase(ExpandConstant('{param:CloseObs|ask}'));
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = PortableDirPage.ID) and IsStandardMode;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  if (CurPageID = PortableDirPage.ID) and (not IsStandardMode) then
  begin
    if not IsObsRootValid(PortableDirPage.Values[0]) then
    begin
      MsgBox('That folder does not look like an OBS Studio root.' + #13#10 + #13#10 +
        'Please choose the folder that contains:' + #13#10 +
        'bin\64bit\obs64.exe', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if IsObsRunning and (not RequestObsClose) then
  begin
    if WizardSilent then
      Result := 'OBS Studio is running. Close it first, or use /CloseObs=yes (normal close) or /CloseObs=force (allow force-close fallback).'
    else
      Result := 'OBS Studio is still running. Close OBS Studio, then click Install again.';
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    RememberInstallTarget;
end;
