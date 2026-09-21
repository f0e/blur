[Files]
; only needed to extract tensorrt, so it's never installed
Source: "{#DepsDir}\7zip\7z.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Components: vstrt
Source: "{#DepsDir}\7zip\7z.dll"; DestDir: "{tmp}"; Flags: deleteafterinstall; Components: vstrt

[Code]
// downloads and extracts vs-mlrt's tensorrt plugins and the rife model when the vstrt component is selected. they're
// too big to bundle. each gets a version marker once it's extracted, so it's skipped if it's already there and
// downloaded again when the version here changes or an earlier extraction didn't finish

#define VsMlrtVersion "v15.16"
#define VsMlrtUrl "https://github.com/AmusementClub/vs-mlrt/releases/download/" + VsMlrtVersion + "/vsmlrt-windows-x64-tensorrt." + VsMlrtVersion + ".7z"
#define VsMlrtSha256Part1 "9fe674f62b9d33a369e7bd6584052986af4c81b12a82b75e5d82aad7c06733cb"
#define VsMlrtSha256Part2 "387b295726bd159f5b4965d0ee8d55bb377f1d8a2add7f623324c60cece44a16"
#define VsMlrtDownloadSize "2676014172"
#define RifeVersion "rife_v4.26"
#define RifeUrl "https://github.com/AmusementClub/vs-mlrt/releases/download/external-models/" + RifeVersion + ".7z"
#define RifeSha256 "dfdabd84a2a3db773f87604b8cc255e94a6a72f13550d910ccd3b4ee2606cd4f"
#define RifeDownloadSize "19591599"

var
  DownloadPage: TDownloadWizardPage;

function TensorRTPluginsDir: String;
begin
  Result := ExpandConstant('{app}\lib\vapoursynth\vs-plugins');
end;

// the downloads go to {tmp}, which setup's own disk space check doesn't cover. they're still there while they're
// extracted into {app}, so both count when it's the same drive
function HasSpaceToDownload(DownloadSize, ExtractedSize: Int64): Boolean;
var
  TmpDrive: String;
  Needed, Free, Total: Int64;
begin
  TmpDrive := ExtractFileDrive(ExpandConstant('{tmp}'));
  Needed := DownloadSize;
  if CompareText(TmpDrive, ExtractFileDrive(ExpandConstant('{app}'))) = 0 then
    Needed := Needed + ExtractedSize;

  Result := not GetSpaceOnDisk64(TmpDrive + '\', Free, Total) or (Free >= Needed);
  if not Result then
    SuppressibleMsgBox('TensorRT RIFE needs ' + IntToStr(Needed div 1073741824 + 1) + ' GB free on ' + TmpDrive +
      ' to download. Free up some space and rerun the installer.', mbError, MB_OK, IDOK);
end;

function IsInstalled(Marker, Version: String): Boolean;
var
  Installed: AnsiString;
begin
  Result := LoadStringFromFile(Marker, Installed) and (Trim(String(Installed)) = Version);
end;

// clears out an older version's plugins (and its marker) so none of its files are left mixed in with the new ones
procedure RemoveTensorRTPlugins;
var
  FindRec: TFindRec;
  Path: String;
begin
  if FindFirst(TensorRTPluginsDir + '\vs*', FindRec) then
  try
    repeat
      Path := TensorRTPluginsDir + '\' + FindRec.Name;
      if FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY <> 0 then
        DelTree(Path, True, True, True)
      else
        DeleteFile(Path);
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

function Extract7z(Archive, DestDir, Switches: String): Boolean;
var
  ResultCode: Integer;
begin
  Result := Exec(ExpandConstant('{tmp}\7z.exe'),
    Format('x "%s" -o"%s" -y %s', [Archive, DestDir, Switches]),
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
end;

function DownloadWithRetry: Boolean;
begin
  Result := False;

  DownloadPage.Show;
  try
    repeat
      try
        DownloadPage.Download;
        Result := True;
      except
        if SuppressibleMsgBox(AddPeriod(GetExceptionMessage), mbError, MB_RETRYCANCEL, IDCANCEL) = IDCANCEL then
          Exit;
      end;
    until Result;
  finally
    DownloadPage.Hide;
  end;
end;

procedure InstallTensorRT;
var
  PluginsMarker, ModelDir, ModelMarker: String;
  NeedPlugins, NeedModel: Boolean;
  DownloadSize, ExtractedSize: Int64;
begin
  PluginsMarker := TensorRTPluginsDir + '\vsmlrt.version';
  ModelDir := TensorRTPluginsDir + '\models\rife_v2';
  ModelMarker := ModelDir + '\rife.version';
  NeedPlugins := not IsInstalled(PluginsMarker, '{#VsMlrtVersion}');
  NeedModel := not IsInstalled(ModelMarker, '{#RifeVersion}');

  if not NeedPlugins and not NeedModel then
    Exit;

  DownloadPage.Clear;
  DownloadSize := 0;
  ExtractedSize := 0;

  if NeedPlugins then
  begin
    DownloadPage.Add('{#VsMlrtUrl}.001', 'vsmlrt.7z.001', '{#VsMlrtSha256Part1}');
    DownloadPage.Add('{#VsMlrtUrl}.002', 'vsmlrt.7z.002', '{#VsMlrtSha256Part2}');
    DownloadSize := DownloadSize + {#VsMlrtDownloadSize};
    ExtractedSize := {#TensorRTSize};
  end;

  if NeedModel then
  begin
    DownloadPage.Add('{#RifeUrl}', 'rife.7z', '{#RifeSha256}');
    DownloadSize := DownloadSize + {#RifeDownloadSize};
  end;

  if not HasSpaceToDownload(DownloadSize, ExtractedSize) then
    Exit;

  if DownloadWithRetry then
  begin
    WizardForm.StatusLabel.Caption := 'Extracting TensorRT RIFE...';

    // the plugins archive bundles every vs-mlrt model, and the model archive has an older copy in rife\ - blur
    // only uses rife_v2
    if NeedPlugins then
    begin
      RemoveTensorRTPlugins;
      if Extract7z(ExpandConstant('{tmp}\vsmlrt.7z.001'), TensorRTPluginsDir, '-x!models') then
        SaveStringToFile(PluginsMarker, '{#VsMlrtVersion}', False);
    end;

    if NeedModel then
    begin
      DelTree(ModelDir, True, True, True);
      if Extract7z(ExpandConstant('{tmp}\rife.7z'), TensorRTPluginsDir + '\models', '-x!rife') then
        SaveStringToFile(ModelMarker, '{#RifeVersion}', False);
    end;
  end;

  if not IsInstalled(PluginsMarker, '{#VsMlrtVersion}') or not IsInstalled(ModelMarker, '{#RifeVersion}') then
    SuppressibleMsgBox('TensorRT RIFE couldn''t be installed. Rerun the installer to try again.', mbError, MB_OK, IDOK);
end;

// vapoursynth loads plugins from subfolders too, and vs-mlrt's are full of cuda and onnxruntime libraries that would
// all get loaded every time vspipe starts. a manifest that lists nothing stops it looking in a folder
procedure WriteTensorRTManifests;
var
  FindRec: TFindRec;
begin
  if FindFirst(TensorRTPluginsDir + '\*', FindRec) then
  try
    repeat
      if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY <> 0) and (FindRec.Name <> '.') and (FindRec.Name <> '..') then
        SaveStringToFile(TensorRTPluginsDir + '\' + FindRec.Name + '\manifest.vs', '[VapourSynth Manifest V1]', False);
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

<event('InitializeWizard')>
procedure TensorRTInitializeWizard;
begin
  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), nil);
end;

<event('CurStepChanged')>
procedure TensorRTCurStepChanged(CurStep: TSetupStep);
begin
  if CurStep <> ssPostInstall then
    Exit;

  if WizardIsComponentSelected('vstrt') then
    InstallTensorRT;

  // also covers tensorrt that's already there from before, whether or not it was selected this time
  WriteTensorRTManifests;
end;
