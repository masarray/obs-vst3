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
Source: "payload\obs-safe-vst3-rack-host.exe"; DestDir: "{code:GetPluginBinDir}"; Flags: ignoreversion
Source: "payload\obs-safe-vst3-scanner.exe"; DestDir: "{code:GetPluginBinDir}"; Flags: ignoreversion
Source: "payload\en-US.ini"; DestDir: "{code:GetPluginLocaleDir}"; Flags: ignoreversion

[InstallDelete]
; Remove only this product from OBS's historical per-machine/per-user plug-in
; roots. The active package is always installed into the explicitly selected
; OBS root below, which works for both normal and portable-mode OBS launches.
Type: filesandordirs; Name: "{commonappdata}\obs-studio\plugins\obs-safe-vst3"
Type: filesandordirs; Name: "{userappdata}\obs-studio\plugins\obs-safe-vst3"
; Also replace an older copy in the selected OBS root without touching any
; other plug-in files in that installation.
Type: files; Name: "{code:GetPluginBinDir}\obs-safe-vst3.dll"
Type: files; Name: "{code:GetPluginBinDir}\obs-safe-vst3-host.exe"
Type: files; Name: "{code:GetPluginBinDir}\obs-safe-vst3-rack-host.exe"
Type: files; Name: "{code:GetPluginBinDir}\obs-safe-vst3-scanner.exe"
Type: filesandordirs; Name: "{code:GetPluginDataDir}"
; If an earlier installer remembered a different valid OBS root, remove only
; this product from that old root before LastObsRoot is updated to the new one.
Type: files; Name: "{code:GetPreviousPluginBinDir}\obs-safe-vst3.dll"; Check: ShouldCleanPreviousObsRoot
Type: files; Name: "{code:GetPreviousPluginBinDir}\obs-safe-vst3-host.exe"; Check: ShouldCleanPreviousObsRoot
Type: files; Name: "{code:GetPreviousPluginBinDir}\obs-safe-vst3-rack-host.exe"; Check: ShouldCleanPreviousObsRoot
Type: files; Name: "{code:GetPreviousPluginBinDir}\obs-safe-vst3-scanner.exe"; Check: ShouldCleanPreviousObsRoot
Type: filesandordirs; Name: "{code:GetPreviousPluginDataDir}"; Check: ShouldCleanPreviousObsRoot

[Code]
const
  SettingsRegKey = 'Software\masarray\OBS Safe VST3 Host';
  LastObsRootValue = 'LastObsRoot';
  LegacyLastPortableDirValue = 'LastPortableObsDir';

var
  ObsRootPage: TInputDirWizardPage;
  ObsRootParam: String;
  LegacyPortableObsDirParam: String;
  CloseObsParam: String;
  PreviousObsRoot: String;

function AddSlash(const Path: String): String;
begin
  Result := Path;
  if (Result <> '') and (Result[Length(Result)] <> '\') then
    Result := Result + '\';
end;

function DefaultObsRoot: String;
begin
  Result := ExpandConstant('{autopf}\obs-studio');
end;

function IsObsRootValid(const Root: String): Boolean;
begin
  Result := (Root <> '') and FileExists(AddSlash(Root) + 'bin\64bit\obs64.exe');
end;

function SelectedObsRoot: String;
begin
  Result := ObsRootPage.Values[0];
end;

function GetPluginBinDir(Param: String): String;
begin
  Result := AddSlash(SelectedObsRoot) + 'obs-plugins\64bit';
end;

function GetPluginDataDir(Param: String): String;
begin
  Result := AddSlash(SelectedObsRoot) + 'data\obs-plugins\obs-safe-vst3';
end;

function GetPluginLocaleDir(Param: String): String;
begin
  Result := GetPluginDataDir('') + '\locale';
end;

function ShouldCleanPreviousObsRoot: Boolean;
begin
  Result := (PreviousObsRoot <> '') and IsObsRootValid(PreviousObsRoot) and
    (CompareText(RemoveBackslashUnlessRoot(PreviousObsRoot),
      RemoveBackslashUnlessRoot(SelectedObsRoot)) <> 0);
end;

function GetPreviousPluginBinDir(Param: String): String;
begin
  Result := AddSlash(PreviousObsRoot) + 'obs-plugins\64bit';
end;

function GetPreviousPluginDataDir(Param: String): String;
begin
  Result := AddSlash(PreviousObsRoot) + 'data\obs-plugins\obs-safe-vst3';
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

function LoadRememberedObsRoot: String;
var
  Remembered: String;
begin
  Result := '';
  Remembered := '';
  if RegQueryStringValue(HKEY_CURRENT_USER, SettingsRegKey,
    LastObsRootValue, Remembered) and IsObsRootValid(Remembered) then
  begin
    Result := Remembered;
    Exit;
  end;

  { Migrate the path remembered by v0.3/v0.4 preview installers. }
  Remembered := '';
  if RegQueryStringValue(HKEY_CURRENT_USER, SettingsRegKey,
    LegacyLastPortableDirValue, Remembered) and IsObsRootValid(Remembered) then
    Result := Remembered;
end;

procedure RememberObsRoot;
begin
  RegWriteStringValue(HKEY_CURRENT_USER, SettingsRegKey,
    LastObsRootValue, SelectedObsRoot);
end;

procedure InitializeWizard;
var
  InitialRoot: String;
begin
  ObsRootPage := CreateInputDirPage(wpWelcome,
    'Select OBS Studio',
    'Choose the OBS Studio root folder',
    'OBS Safe VST3 Host installs directly into the selected OBS root. ' +
    'This is the same plug-in layout for normal, Steam, custom and portable-mode OBS. ' +
    'Choose the folder that contains bin\64bit\obs64.exe.',
    False, '');
  ObsRootPage.Add('');

  PreviousObsRoot := LoadRememberedObsRoot;
  InitialRoot := PreviousObsRoot;
  if InitialRoot = '' then
  begin
    if IsObsRootValid(DefaultObsRoot) then
      InitialRoot := DefaultObsRoot
    else
      InitialRoot := ExpandConstant('{sd}\OBS Studio');
  end;

  { New explicit target takes precedence. Keep /PortableObsDir as a backwards-
    compatible alias for automation/scripts from earlier preview installers. }
  ObsRootParam := ExpandConstant('{param:ObsRoot|}');
  LegacyPortableObsDirParam := ExpandConstant('{param:PortableObsDir|}');
  if ObsRootParam <> '' then
    InitialRoot := ObsRootParam
  else if LegacyPortableObsDirParam <> '' then
    InitialRoot := LegacyPortableObsDirParam;

  ObsRootPage.Values[0] := InitialRoot;
  CloseObsParam := Lowercase(ExpandConstant('{param:CloseObs|ask}'));
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = ObsRootPage.ID then
  begin
    if not IsObsRootValid(SelectedObsRoot) then
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
  if not IsObsRootValid(SelectedObsRoot) then
  begin
    Result := 'A valid OBS Studio root is required. Select the folder containing bin\64bit\obs64.exe.';
    Exit;
  end;

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
    RememberObsRoot;
end;
