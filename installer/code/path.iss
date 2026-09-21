[Code]
// adds {app} to PATH when the envPath task is selected, and takes it back out on uninstall

function EnvironmentRoot: Integer;
begin
  if IsAdminInstallMode then
    Result := HKLM
  else
    Result := HKCU;
end;

function EnvironmentKey: String;
begin
  if IsAdminInstallMode then
    Result := 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
  else
    Result := 'Environment';
end;

procedure AddToPath(Dir: String);
var
  Paths: String;
begin
  if not RegQueryStringValue(EnvironmentRoot, EnvironmentKey, 'Path', Paths) then
    Paths := '';

  if Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Paths) + ';') > 0 then
    Exit;

  if (Paths <> '') and (Paths[Length(Paths)] <> ';') then
    Paths := Paths + ';';

  RegWriteExpandStringValue(EnvironmentRoot, EnvironmentKey, 'Path', Paths + Dir);
end;

procedure RemoveFromPath(Dir: String);
var
  Paths, Entry, Kept: String;
  P: Integer;
  Removed: Boolean;
begin
  if not RegQueryStringValue(EnvironmentRoot, EnvironmentKey, 'Path', Paths) then
    Exit;

  Paths := Paths + ';';
  Kept := '';
  Removed := False;

  while Paths <> '' do
  begin
    P := Pos(';', Paths);
    Entry := Copy(Paths, 1, P - 1);
    Delete(Paths, 1, P);

    if CompareText(RemoveBackslashUnlessRoot(Entry), RemoveBackslashUnlessRoot(Dir)) = 0 then
      Removed := True
    else if Entry <> '' then
    begin
      if Kept <> '' then
        Kept := Kept + ';';
      Kept := Kept + Entry;
    end;
  end;

  if Removed then
    RegWriteExpandStringValue(EnvironmentRoot, EnvironmentKey, 'Path', Kept);
end;

<event('CurStepChanged')>
procedure PathCurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('envPath') then
    AddToPath(ExpandConstant('{app}'));
end;

<event('CurUninstallStepChanged')>
procedure PathCurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath(ExpandConstant('{app}'));
end;
