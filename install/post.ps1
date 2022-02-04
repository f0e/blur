Rename-Item -Path "$DIR\Smoothie*" -NewName "Smoothie"

if (-Not(Test-Path "$ScoopDir\shims\Smoothie.exe")){
    Copy-Item "$ScoopDir\shims\7z.exe" "$ScoopDir\shims\Smoothie.exe" # All shims are the same
}
if (-Not(Test-Path "$ScoopDir\shims\Smoothie.shim")){

    New-Item "$ScoopDir\shims\Smoothie.shim" -Force
    Add-Content "$ScoopDir\shims\Smoothie.shim" "path = `"$DIR\python.exe`"$([Environment]::NewLine)args = `"$DIR\Smoothie\Smoothie.py`""
}

# havsfunc, mvsfunc, filldrops & adjust 
Move-Item "$DIR\Smoothie\*.py" "$DIR\vapoursynth64\plugins"

# vs-frameblender
Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/f0e/vs-frameblender/releases/latest/download/vs-frameblender-x64.dll' -OutFile "$DIR\vapoursynth64\plugins\vs-frameblender-x64.dll"

# ffms2 & libmv
& "$DIR\python.exe" "$DIR\vsrepo.py" update
& "$DIR\python.exe" "$DIR\vsrepo.py" install ffms2 mv

# SVPFlow
Remove-Item "$env:TMP\SVPFlow.7z","$env:TMP\SVPFlow" -Force -Ea Ignore -Recurse
Invoke-WebRequest -UseBasicParsing -Uri 'https://raw.githubusercontent.com/bjaan/smoothvideo/main/SVPflow_LastGoodVersions.7z' -OutFile "$env:TMP\SVPFlow.7z"
while (-not(Test-Path "$env:TMP\SVPFlow.7z")){$null}
7z x  "$env:TMP\SVPFlow.7z" -o"$env:TMP\SVPFlow"
Move-Item "$env:TEMP\svpflow\x64_vs\*.dll" "$DIR\vapoursynth64\plugins"
Remove-Item "$env:TMP\SVPFlow.7z","$env:TMP\SVPFlow" -Force -Ea Ignore -Recurse