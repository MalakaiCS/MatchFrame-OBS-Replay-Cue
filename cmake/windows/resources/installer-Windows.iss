; Replay Cue for OBS Studio: the Windows installer (Inno Setup 6)
;
; Built by .github/scripts/Package-Windows.ps1 from the files `cmake --install`
; puts in release\<Configuration>\obs-replay-cue, which passes:
;   ProductName, DisplayName, ProductVersion, FileVersion, Author, Website (buildspec.json)
;   ObsMajor, ObsMinor (the OBS Studio version it is built against)
;   SourceDir, OutputDir, OutputBaseName

#define AppName DisplayName + " for OBS Studio"

[Setup]
AppId={{899FA952-D974-4834-A8DF-D22D14319C00}
AppName={#AppName}
AppVersion={#ProductVersion}
AppVerName={#AppName} {#ProductVersion}
AppPublisher={#Author}
AppPublisherURL={#Website}
AppSupportURL={#Website}/issues
AppUpdatesURL={#Website}/releases
AppCopyright=Copyright (C) 2026 Replay Cue contributors
VersionInfoVersion={#FileVersion}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} Setup
; where OBS Studio looks for plugins installed apart from it
DefaultDirName={commonappdata}\obs-studio\plugins\{#ProductName}
DisableDirPage=yes
AlwaysShowDirOnReadyPage=yes
UsePreviousAppDir=no
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
UninstallDisplayName={#AppName}
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
; the Ready page is the first one: there is nothing to go Back to
ReadyLabel2a=Click Install to continue with the installation.
FinishedLabelNoIcons=Setup has finished installing [name].%n%nIn OBS Studio, open Docks > {#DisplayName}.

[CustomMessages]
ObsRunning=OBS Studio is running.%n%nClose OBS Studio, then click Retry.
ObsMissing=OBS Studio was not found on this computer.%n%n{#DisplayName} is a plugin for OBS Studio {#ObsMajor}.{#ObsMinor} or newer: install OBS Studio from obsproject.com first.%n%nIf OBS Studio is installed (from Steam, for example), click Yes to install {#DisplayName} anyway.
ObsVersion={#DisplayName} {#ProductVersion} works with OBS Studio {#ObsMajor}.{#ObsMinor} or a newer {#ObsMajor}.x version, and OBS Studio %1 is installed: {#DisplayName} may not load in it, or not record and play replays.%n%nUpdate OBS Studio (or look for a {#DisplayName} release for your version) first.%n%nInstall {#DisplayName} anyway?
LaunchObs=Start OBS Studio

[InstallDelete]
; what an earlier version (or the .zip) left behind
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\data"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
Filename: "{code:GetObsFolder}\bin\64bit\obs64.exe"; WorkingDir: "{code:GetObsFolder}\bin\64bit"; Description: "{cm:LaunchObs}"; Flags: postinstall nowait skipifsilent; Check: ObsFound

[Code]
function IsObsRunning(): Boolean;
var
  Locator, Service, Processes: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Processes := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''obs64.exe''');
    Result := Processes.Count > 0;
  except
    Log('Could not check whether OBS Studio is running: ' + GetExceptionMessage);
  end;
end;

{ OBS Studio holds the plugin while it runs: asks until it is closed (True)
  or the user gives up (False) }
function ObsClosed(): Boolean;
begin
  Result := True;
  while IsObsRunning() do
    if SuppressibleMsgBox(CustomMessage('ObsRunning'), mbError, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY then
    begin
      Result := False;
      Exit;
    end;
end;

{ Where the OBS Studio installer put it, or '' }
function ObsFolder(): String;
begin
  if not RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Result) then
    if not RegQueryStringValue(HKLM32, 'SOFTWARE\OBS Studio', '', Result) then
      Result := '';
end;

function GetObsFolder(Param: String): String;
begin
  Result := ObsFolder();
end;

function ObsFound(): Boolean;
begin
  Result := FileExists(ObsFolder() + '\bin\64bit\obs64.exe');
end;

{ "32.2.1", or '' }
function ObsVersion(): String;
var
  Key: String;
begin
  Key := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio';
  if not RegQueryStringValue(HKLM64, Key, 'DisplayVersion', Result) then
    if not RegQueryStringValue(HKLM32, Key, 'DisplayVersion', Result) then
      Result := '';
end;

{ "2-beta1" -> 2, "beta" -> -1 }
function LeadingNumber(const S: String): Integer;
var
  Digits: Integer;
begin
  Digits := 0;
  while Digits < Length(S) do
  begin
    if (S[Digits + 1] < '0') or (S[Digits + 1] > '9') then
      Break;
    Digits := Digits + 1;
  end;
  Result := StrToIntDef(Copy(S, 1, Digits), -1);
end;

function InitializeSetup(): Boolean;
var
  Version: String;
  Dot, Major, Minor: Integer;
begin
  Result := True;
  Version := ObsVersion();
  if Version = '' then
  begin
    if ObsFolder() = '' then
      Result := SuppressibleMsgBox(CustomMessage('ObsMissing'), mbConfirmation, MB_YESNO, IDYES) = IDYES;
    Exit;
  end;

  Major := LeadingNumber(Version);
  Minor := -1;
  Dot := Pos('.', Version);
  if Dot > 0 then
    Minor := LeadingNumber(Copy(Version, Dot + 1, Length(Version)));
  Log(Format('OBS Studio %s is installed in %s', [Version, ObsFolder()]));
  if (Major <> {#ObsMajor}) or (Minor < {#ObsMinor}) then
    Result := SuppressibleMsgBox(FmtMessage(CustomMessage('ObsVersion'), [Version]), mbConfirmation, MB_YESNO,
                                 IDYES) = IDYES;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpReady then
    Result := ObsClosed();
end;

function InitializeUninstall(): Boolean;
begin
  Result := ObsClosed();
end;
