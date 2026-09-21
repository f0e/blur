// installs the msvc runtime vapoursynth and some plugins need, if what's installed is missing or older than the redist
// bundled from ci/build-dependencies-windows.ps1

#define VCRedistMajor 14
#define VCRedistMinor 51
#define VCRedistBld 36247

[Files]
Source: "{#DepsDir}\redist\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: VCRedistNeeded

[Run]
; shellexec so it can elevate itself when setup isn't running as admin
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Microsoft Visual C++ runtime..."; Flags: shellexec waituntilterminated; Check: VCRedistNeeded

[Code]
function VCRedistNeeded: Boolean;
var
  Key: String;
  Installed, Major, Minor, Bld: Cardinal;
begin
  Key := 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  if not RegQueryDWordValue(HKLM64, Key, 'Installed', Installed) or (Installed <> 1) or
     not RegQueryDWordValue(HKLM64, Key, 'Major', Major) or
     not RegQueryDWordValue(HKLM64, Key, 'Minor', Minor) or
     not RegQueryDWordValue(HKLM64, Key, 'Bld', Bld) then
  begin
    Result := True;
    Exit;
  end;

  if Major <> {#VCRedistMajor} then
    Result := Major < {#VCRedistMajor}
  else if Minor <> {#VCRedistMinor} then
    Result := Minor < {#VCRedistMinor}
  else
    Result := Bld < {#VCRedistBld};
end;
