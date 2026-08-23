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
Source: "payload\en-US.ini"; DestDir: "{code:GetPluginLocaleDir}"; Flags: ignoreversion

[Code]
var
  InstallModePage: TInputOptionWizardPage;
  PortableDirPage: TInputDirWizardPage;
  PortableObsDirParam: String;

function AddSlash(const Path: String): String;
begin
  Result := AddBackslash(RemoveBackslashUnlessRoot(Path));
end;

function IsStandardMode: Boolean;
begin
  Result := InstallModePage.SelectedValueIndex = 0;
end;

function IsObsRootValid(const Root: String): Boolean;
begin
  Result := FileExists(AddSlash(Root) + 'bin\64bit\obs64.exe');
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

procedure InitializeWizard;
var
  DefaultPortablePath: String;
begin
  InstallModePage := CreateInputOptionPage(wpWelcome,
    'Choose OBS installation type',
    'Where should OBS Safe VST3 Host be installed?',
    'For a normal OBS Studio installation, keep the recommended option. ' +
    'Choose Custom / Portable only when you run OBS from a self-contained folder.',
    True, False);

  InstallModePage.Add('Standard OBS Studio (recommended)');
  InstallModePage.Add('Custom / Portable OBS folder');
  InstallModePage.SelectedValueIndex := 0;

  PortableDirPage := CreateInputDirPage(InstallModePage.ID,
    'Select your OBS folder',
    'Choose the OBS Studio root folder',
    'Select the folder that contains bin\64bit\obs64.exe. ' +
    'Setup will install the plug-in into this OBS tree.',
    False, '');
  PortableDirPage.Add('');

  DefaultPortablePath := ExpandConstant('{autopf}\obs-studio');
  if IsObsRootValid(DefaultPortablePath) then
    PortableDirPage.Values[0] := DefaultPortablePath
  else
    PortableDirPage.Values[0] := ExpandConstant('{sd}\OBS Studio');

  PortableObsDirParam := ExpandConstant('{param:PortableObsDir|}');
  if PortableObsDirParam <> '' then
  begin
    InstallModePage.SelectedValueIndex := 1;
    PortableDirPage.Values[0] := PortableObsDirParam;
  end;
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
  if IsObsRunning then
    Result := 'OBS Studio is still running. Close OBS Studio completely, then click Install again.';
end;
