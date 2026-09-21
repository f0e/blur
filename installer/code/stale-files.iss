[Code]
// clears out lib when installing over an existing install, so nothing an older version shipped is left behind -
// vapoursynth loads every plugin in vs-plugins, so a leftover one would still get loaded. tensorrt is downloaded
// rather than installed, so it stays

// vs-mlrt names everything it puts in the plugins dir vs*, apart from models
function IsTensorRTFile(Name: String): Boolean;
begin
  Result := (CompareText(Copy(Name, 1, 2), 'vs') = 0) or (CompareText(Name, 'models') = 0);
end;

procedure ClearDir(Dir: String);
var
  FindRec: TFindRec;
  Path: String;
begin
  if not FindFirst(Dir + '\*', FindRec) then
    Exit;

  try
    repeat
      if (FindRec.Name = '.') or (FindRec.Name = '..') then
        Continue;

      Path := Dir + '\' + FindRec.Name;

      // the dirs on the way to tensorrt are cleared rather than removed
      if (CompareText(Path, ExpandConstant('{app}\lib\vapoursynth')) = 0) or
         (CompareText(Path, TensorRTPluginsDir) = 0) then
        ClearDir(Path)
      else if (CompareText(Dir, TensorRTPluginsDir) <> 0) or not IsTensorRTFile(FindRec.Name) then
      begin
        if FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY <> 0 then
          DelTree(Path, True, True, True)
        else
          DeleteFile(Path);
      end;
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

<event('CurStepChanged')>
procedure StaleFilesCurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    ClearDir(ExpandConstant('{app}\lib'));
end;
