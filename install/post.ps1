Rename-Item -Path (Convert-Path "$DIR\Smoothie*") -NewName "Smoothie"

if (-Not(Test-Path "$ScoopDir\shims\sm.exe")){
    Copy-Item "$ScoopDir\shims\7z.exe" "$ScoopDir\shims\sm.exe" # All shims are the same
}
if (-Not(Test-Path "$ScoopDir\shims\sm.shim")){

    New-Item "$ScoopDir\shims\sm.shim" -Force
    Add-Content "$ScoopDir\shims\sm.shim" "path = `"$DIR\python.exe`"$([Environment]::NewLine)args = `"$DIR\Smoothie\Smoothie.py`""
}

Write-Warning "Importing havsfunc,mvsfunc, filldrops & adjust"
Move-Item "$DIR\Smoothie\plugins\*.py" "$DIR"

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
Move-Item "$env:TEMP\svpflow\x64_vs\*.dll" "$DIR\vapoursynth64\plugins" -Force
Remove-Item "$env:TMP\SVPFlow.7z","$env:TMP\SVPFlow" -Force -Ea Ignore -Recurse

Write-Warning "Testing FFmpeg encoders.."
@(
    'hevc_nvenc -rc constqp -preset p7 -qp 18'
    'h264_nvenc -rc constqp -preset p7 -qp 15'
    'hevc_amf -quality quality -qp_i 16 -qp_p 18 -qp_b 20'
    'h264_amf -quality quality -qp_i 12 -qp_p 12 -qp_b 12'
    'hevc_qsv -preset veryslow -global_quality:v 18'
    'h264_qsv -preset veryslow -global_quality:v 15'
    'libx265 -preset medium -crf 18'
    'libx264 -preset slow -crf 15'
) | ForEach-Object -Begin {
    $script:shouldStop = $false
} -Process {
    if ($shouldStop -eq $true) { return }
    Invoke-Expression "ffmpeg.exe -loglevel fatal -f lavfi -i nullsrc=3840x2160 -t 0.1 -c:v $_ -f null NUL"
    if ($LASTEXITCODE -eq 0){
        $script:valid_args = $_
        $shouldStop = $true # Crappy way to stop the loop since most people that'll execute this will technically be parsing the raw URL as a scriptblock

    }
}

(Get-Content "$DIR\Smoothie\settings\recipe.ini") -replace ('-c:v libx264 -preset slow -crf 15',"-c:v $valid_args") | Set-Content "$DIR\Smoothie\settings\recipe.ini"