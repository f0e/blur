#define MyAppName "blur"
#define MyAppPublisher "tekno"
#define MyAppURL "https://f0e.github.io/blur"
#define MyAppExeName "blur-gui.exe"
#define MyAppId "D283CF94-CD1F-432D-B4BE-0516562C258B"

; installs straight from the win-release build and ci/build-dependencies-windows.ps1's output. pass /DDepsDir= to use
; other dependencies
#define BinDir "..\bin\Release"
#ifndef DepsDir
  #define DepsDir "..\ci\out"
#endif

; the version comes from the built exe's VERSIONINFO, which cmake fills in from project(blur VERSION ...)
#define MainExePath AddBackslash(AddBackslash(SourcePath) + BinDir) + MyAppExeName
#if !FileExists(MainExePath)
  #error Couldn't find blur-gui.exe in bin/Release, build blur with the win-release preset first
#endif
#define MyAppVersion GetStringFileInfo(MainExePath, "FileVersion")
#if MyAppVersion == ""
  #error blur-gui.exe has no version info, rebuild it
#endif

; the space tensorrt takes up once it's extracted
#define TensorRTSize "3757471855"

[Setup]
AppId={{{#MyAppId}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
; also makes x64compatible the default for ArchitecturesAllowed and ArchitecturesInstallIn64BitMode
SetupArchitecture=x64
ChangesEnvironment=yes
; the finish page offers to launch blur, restarting it as well would open it twice
RestartApplications=no
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupIconFile=..\resources\blur.ico
WizardStyle=modern
WizardImageFile=resources\wizard-large.png
WizardSmallImageFile=resources\wizard-small.png
Compression=lzma2
SolidCompression=yes
SetupLogging=yes
OutputDir=output
OutputBaseFilename=blur-installer

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Full installation"
Name: "compact"; Description: "Compact installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "main"; Description: "blur (required)"; Types: full compact custom; Flags: fixed
Name: "vstrt"; Description: "NVIDIA TensorRT RIFE interpolation (~2.5GB download)"; Types: full; ExtraDiskSpaceRequired: {#TensorRTSize}

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "envPath"; Description: "Add to PATH"; GroupDescription: "Other:"; Flags: unchecked

[Files]
Source: "{#BinDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\blur-cli.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\libmpv-2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\libEGL.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\libGLESv2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DepsDir}\*"; DestDir: "{app}\lib"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\src\vapoursynth\*"; DestDir: "{app}\lib"; Excludes: "__pycache__,*.pyc"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; tensorrt is downloaded after install so the uninstaller doesn't know about it
Type: filesandordirs; Name: "{app}\lib"

; migrate-32bit uses path's functions, stale-files uses tensorrt's
#include "code\path.iss"
#include "code\migrate-32bit.iss"
#include "code\tensorrt.iss"
#include "code\stale-files.iss"

[Code]
var
  UpdateMode: Boolean;

function HasParam(Name: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), Name) = 0 then
    begin
      Result := True;
      Exit;
    end;
end;

function InitializeSetup: Boolean;
begin
  // passed by blur's updater, which only needs the install and finish pages
  UpdateMode := HasParam('/UPDATE');
  Result := True;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := UpdateMode and
    ((PageID = wpSelectDir) or (PageID = wpSelectComponents) or (PageID = wpSelectTasks) or (PageID = wpReady));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  SettingsDir: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;

  SettingsDir := ExpandConstant('{userappdata}\{#MyAppName}');
  if DirExists(SettingsDir) and
     (SuppressibleMsgBox('Also remove your blur settings, configs, masks and caches?' + #13#10#13#10 + SettingsDir,
       mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES) then
    DelTree(SettingsDir, True, True, True);
end;
