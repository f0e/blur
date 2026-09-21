[Code]
// moves installs from before the installer went 64-bit out of Program Files (x86). can be deleted once nobody's
// updating from those versions any more
//
// they're in the 32-bit registry view, where a 64-bit install no longer looks for previous installs. copying their
// choices into the 64-bit view lets setup pick them up as if they were a previous 64-bit install. the old install is
// only removed once the new one is in, unless it's in the same dir, where its uninstaller would take the new files
// with it

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
  // the value rather than the key, so a key left behind by a killed setup doesn't stop the migration
  if not IsAdminInstallMode or RegValueExists(HKLM64, UninstallKey, 'UninstallString') or
     not RegQueryStringValue(HKLM32, UninstallKey, 'Inno Setup: App Path', LegacyDir) then
    Exit;

  LegacyDir := RemoveBackslashUnlessRoot(LegacyDir);
  LegacyDirIsDefault := CompareText(LegacyDir, ExpandConstant('{commonpf32}\{#MyAppName}')) = 0;

  // installs in the old default dir move to the new one, custom dirs stay put
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

  // in case the old uninstaller is missing or failed, so it doesn't show up twice in installed apps
  RegDeleteKeyIncludingSubkeys(HKLM32, UninstallKey);
  LegacyRemoved := True;
end;

// the old uninstaller can't be used once the new install is in - its shortcuts have the same names as the new
// ones, so it'd delete those. everything else it would do is done here instead
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
  // don't leave copied choices behind if setup didn't finish, unless the old install is already gone and they're all
  // that's left of it
  if (LegacyDir <> '') and not LegacyRemoved and not RegValueExists(HKLM64, UninstallKey, 'UninstallString') then
    RegDeleteKeyIncludingSubkeys(HKLM64, UninstallKey);
end;
