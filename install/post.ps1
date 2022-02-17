Rename-Item -Path (Convert-Path "$DIR\Smoothie*") -NewName "Smoothie"

if (-Not(Test-Path "$ScoopDir\shims\sm.exe")){
    Copy-Item "$ScoopDir\shims\7z.exe" "$ScoopDir\shims\sm.exe" # All shims are the same
}
if (-Not(Test-Path "$ScoopDir\shims\sm.shim")){

    New-Item "$ScoopDir\shims\sm.shim" -Force
    Add-Content "$ScoopDir\shims\sm.shim" "path = `"$DIR\VapourSynth\python.exe`"$([Environment]::NewLine)args = `"$DIR\Smoothie\Smoothie.py`""
}

Write-Warning "Testing FFmpeg encoders.. note NVENC may fail if you use an older driver / older FFmpeg version"
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

$rc = (Get-Content "$DIR\Smoothie\settings\recipe.ini") -replace ('libx264 -preset slow -crf 15',$valid_args)

if ($valid_args -like "*libx26*"){
    $rc = $rc -replace ('gpu=true','gpu=false')
}

Set-Content "$DIR\Smoothie\settings\recipe.ini" -Value $rc

$SendTo = [System.Environment]::GetFolderPath('SendTo')

Remove-Item "$SendTo\Smoothie.lnk" -Force -Ea Ignore

if (-Not(Test-Path "$DIR\sm.ico")){
    Invoke-WebRequest -UseBasicParsing https://cdn.discordapp.com/attachments/885925073851146310/940709737685725276/sm.ico -OutFile "$DIR\sm.ico"
}
$WScriptShell = New-Object -ComObject WScript.Shell
$Shortcut = $WScriptShell.CreateShortcut("$SendTo\smoothie.lnk")
$Shortcut.TargetPath = "$DIR\VapourSynth\python.exe"
$Shortcut.Arguments = "$DIR\Smoothie\smoothie.py -input"
$Shortcut.IconLocation = "$DIR\sm.ico"
$Shortcut.Save()
Rename-Item -Path "$SendTo\smoothie.lnk" -NewName 'Smoothie.lnk' # Shortcuts are always created in lowercase for some reason


