[Code]
// downloads and extracts vs-mlrt's tensorrt plugins and the rife model when the vstrt component is selected. they're
// too big to bundle, and skipped if they're already there

#define VsMlrtUrl "https://github.com/AmusementClub/vs-mlrt/releases/download/v15.16/vsmlrt-windows-x64-tensorrt.v15.16.7z"
#define VsMlrtSha256Part1 "9fe674f62b9d33a369e7bd6584052986af4c81b12a82b75e5d82aad7c06733cb"
#define VsMlrtSha256Part2 "387b295726bd159f5b4965d0ee8d55bb377f1d8a2add7f623324c60cece44a16"
#define RifeUrl "https://github.com/AmusementClub/vs-mlrt/releases/download/external-models/rife_v4.26.7z"
#define RifeSha256 "dfdabd84a2a3db773f87604b8cc255e94a6a72f13550d910ccd3b4ee2606cd4f"

var
  DownloadPage: TDownloadWizardPage;

function Extract7z(Archive, DestDir, Switches: String): Boolean;
var
  ResultCode: Integer;
begin
  Result := Exec(ExpandConstant('{app}\lib\vapoursynth\7z.exe'),
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
  PluginsDir: String;
  NeedPlugins, NeedModel: Boolean;
begin
  PluginsDir := ExpandConstant('{app}\lib\vapoursynth\vs-plugins');
  NeedPlugins := not FileExists(PluginsDir + '\vstrt.dll');
  NeedModel := not DirExists(PluginsDir + '\models\rife_v2');

  if not NeedPlugins and not NeedModel then
    Exit;

  DownloadPage.Clear;

  if NeedPlugins then
  begin
    DownloadPage.Add('{#VsMlrtUrl}.001', 'vsmlrt.7z.001', '{#VsMlrtSha256Part1}');
    DownloadPage.Add('{#VsMlrtUrl}.002', 'vsmlrt.7z.002', '{#VsMlrtSha256Part2}');
  end;

  if NeedModel then
    DownloadPage.Add('{#RifeUrl}', 'rife.7z', '{#RifeSha256}');

  if DownloadWithRetry then
  begin
    WizardForm.StatusLabel.Caption := 'Extracting TensorRT RIFE...';

    // the plugins archive bundles every vs-mlrt model, and the model archive has an older copy in rife\ - blur
    // only uses rife_v2
    if NeedPlugins then
      Extract7z(ExpandConstant('{tmp}\vsmlrt.7z.001'), PluginsDir, '-x!models');

    if NeedModel then
      Extract7z(ExpandConstant('{tmp}\rife.7z'), PluginsDir + '\models', '-x!rife');
  end;

  if not FileExists(PluginsDir + '\vstrt.dll') or not DirExists(PluginsDir + '\models\rife_v2') then
    SuppressibleMsgBox('TensorRT RIFE couldn''t be installed. Rerun the installer to try again.', mbError, MB_OK, IDOK);
end;

<event('InitializeWizard')>
procedure TensorRTInitializeWizard;
begin
  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), nil);
end;

<event('CurStepChanged')>
procedure TensorRTCurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsComponentSelected('vstrt') then
    InstallTensorRT;
end;
