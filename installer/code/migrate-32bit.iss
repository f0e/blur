[Code]
// moves pre 64-bit installs out of Program Files (x86). can be deleted once nobody's updating from those versions
//
// copies their choices from the 32-bit registry view into the 64-bit one so setup treats them as a previous install.
// the old install is removed after the new one is in, unless they share a dir

const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{{#MyAppId}}_is1';

var
  LegacyDir: String;
  LegacyDirIsDefault, LegacyRemoved: Boolean;

procedure CopyLegacyValue(Name: String);
var
  Value: String;
begin
  if RegQueryStringValue(HKLM32, UninstallKey, Name, Value) then
    RegWriteStringValue(HKLM64, UninstallKey, Name, Value);
end;

procedure FindLegacyInstall;
begin
  // check the value rather than the key, in case a killed setup left the key behind
  if not IsAdminInstallMode or RegValueExists(HKLM64, UninstallKey, 'UninstallString') or
     not RegQueryStringValue(HKLM32, UninstallKey, 'Inno Setup: App Path', LegacyDir) then
    Exit;

  LegacyDir := RemoveBackslashUnlessRoot(LegacyDir);
  LegacyDirIsDefault := CompareText(LegacyDir, ExpandConstant('{commonpf32}\{#MyAppName}')) = 0;

  // installs in the old default dir move to the new one, custom dirs stay
  if not LegacyDirIsDefault then
    CopyLegacyValue('Inno Setup: App Path');

  CopyLegacyValue('Inno Setup: Setup Type');
  CopyLegacyValue('Inno Setup: Selected Components');
  CopyLegacyValue('Inno Setup: Deselected Components');
  CopyLegacyValue('Inno Setup: Selected Tasks');
  CopyLegacyValue('Inno Setup: Deselected Tasks');
end;

function MovingFromLegacyDir: Boolean;
begin
  Result := LegacyDirIsDefault and (CompareText(LegacyDir, ExpandConstant('{app}')) <> 0);
end;

procedure UninstallLegacy;
var
  Uninstaller: String;
  ResultCode: Integer;
begin
  if RegQueryStringValue(HKLM32, UninstallKey, 'UninstallString', Uninstaller) then
    Exec(RemoveQuotes(Uninstaller), '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);

  // in case the old uninstaller is missing or failed
  RegDeleteKeyIncludingSubkeys(HKLM32, UninstallKey);
  LegacyRemoved := True;
end;

// not using the old uninstaller since it would delete the new shortcuts (same names)
procedure RemoveMovedLegacyInstall;
begin
  RemoveFromPath(LegacyDir);
  RegDeleteKeyIncludingSubkeys(HKLM32, UninstallKey);
  DelTree(LegacyDir, True, True, True);
  LegacyRemoved := True;
end;

<event('InitializeSetup')>
function MigrateInitializeSetup: Boolean;
begin
  FindLegacyInstall;
  Result := True;
end;

// the running app has to close before its files can go
<event('RegisterExtraCloseApplicationsResources')>
procedure MigrateRegisterExtraCloseApplicationsResources;
begin
  if LegacyDir <> '' then
  begin
    RegisterExtraCloseApplicationsResource(LegacyDir + '\{#MyAppExeName}');
    RegisterExtraCloseApplicationsResource(LegacyDir + '\blur-cli.exe');
  end;
end;

<event('CurStepChanged')>
procedure MigrateCurStepChanged(CurStep: TSetupStep);
begin
  if LegacyDir = '' then
    Exit;

  if (CurStep = ssInstall) and not MovingFromLegacyDir then
    UninstallLegacy
  else if (CurStep = ssPostInstall) and MovingFromLegacyDir then
    RemoveMovedLegacyInstall;
end;

<event('DeinitializeSetup')>
procedure MigrateDeinitializeSetup;
begin
  // remove copied choices if setup didn't finish, unless the old install is already gone
  if (LegacyDir <> '') and not LegacyRemoved and not RegValueExists(HKLM64, UninstallKey, 'UninstallString') then
    RegDeleteKeyIncludingSubkeys(HKLM64, UninstallKey);
end;
