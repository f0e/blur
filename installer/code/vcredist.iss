[Code]
// the bundled visual c++ runtime only gets installed if what's already there is older

const
  VCRuntimeKey = 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';

function VCRedistNeeded: Boolean;
var
  Installed, Major, Minor, Bld: Cardinal;
  FileMajor, FileMinor, FileRevision, FileBuild: Word;
begin
  Result := True;

  if not GetVersionComponents(ExpandConstant('{tmp}\VC_redist.x64.exe'), FileMajor, FileMinor, FileRevision, FileBuild) then
    Exit;

  if RegQueryDWordValue(HKLM64, VCRuntimeKey, 'Installed', Installed) and (Installed = 1) and
     RegQueryDWordValue(HKLM64, VCRuntimeKey, 'Major', Major) and
     RegQueryDWordValue(HKLM64, VCRuntimeKey, 'Minor', Minor) and
     RegQueryDWordValue(HKLM64, VCRuntimeKey, 'Bld', Bld) then
    Result := ComparePackedVersion(
      PackVersionComponents(Major, Minor, Bld, 0),
      PackVersionComponents(FileMajor, FileMinor, FileRevision, 0)) < 0;
end;
