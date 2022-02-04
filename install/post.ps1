Rename-Item -Path "$DIR\Smoothie*" -NewName "Smoothie"

if (-Not(Test-Path "$ScoopDir\shims\sm.exe")){
    Copy-Item "$ScoopDir\shims\7z.exe" "$ScoopDir\shims\sm.exe" # All shims are the same
}
if (-Not(Test-Path "$ScoopDir\shims\sm.shim")){

    New-Item "$ScoopDir\shims\sm.shim" -Force
    Add-Content "$ScoopDir\shims\sm.shim" "path = `"$DIR\python.exe`"$([Environment]::NewLine)args = `"$DIR\Smoothie\Smoothie.py`""
}

Write-Warning "Importing havsfunc,mvsfunc, filldrops & adjust"
Move-Item "$DIR\Smoothie\*.py" "$DIR\vapoursynth64\plugins"

Write-Warning "Downloading vs-frameblender"
Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/f0e/vs-frameblender/releases/latest/download/vs-frameblender-x64.dll' -OutFile "$DIR\vapoursynth64\plugins\vs-frameblender-x64.dll"

Write-Warning "Updating vsrepo"
& "$DIR\python.exe" "$DIR\vsrepo.py" update
Write-Warning "Installing ffms2 & libmvtools"
& "$DIR\python.exe" "$DIR\vsrepo.py" install ffms2 mv

Write-Warning "Downloading, extracting & installing SVPFlow"
Remove-Item "$env:TMP\SVPFlow.7z","$env:TMP\SVPFlow" -Force -Ea Ignore -Recurse
Invoke-WebRequest -UseBasicParsing -Uri 'https://raw.githubusercontent.com/bjaan/smoothvideo/main/SVPflow_LastGoodVersions.7z' -OutFile "$env:TMP\SVPFlow.7z"
while (-not(Test-Path "$env:TMP\SVPFlow.7z")){$null}
7z x  "$env:TMP\SVPFlow.7z" -o"$env:TMP\SVPFlow"
Move-Item "$env:TEMP\svpflow\x64_vs\*.dll" "$DIR\vapoursynth64\plugins"
Remove-Item "$env:TMP\SVPFlow.7z","$env:TMP\SVPFlow" -Force -Ea Ignore -Recurse