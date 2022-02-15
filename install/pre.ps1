if (Get-Command sm  -Ea Ignore){
@"

A Smoothie installation has been detected, what would you like to do?

Press R to Reinstall Smoothie (clean install)
Press V to update Smoothie to a newer version
Press U to uninstall Smoothie
"@

function CheckProcesses{
    if (Get-Process -Name sm,vspipe,ffmpeg -ErrorAction SilentlyContinue){
        Write-Warning "One of the following processes are still running and may cause uninstallation issues, make sure these processes are not tied to Smoothie:"
        Get-Process -Name sm,vspipe,ffmpeg -ErrorAction SilentlyContinue
    }     

}
switch (choice.exe /C RVU /N | Out-Null){
    1{
        CheckProcesses
        scoop.cmd uninstall smoothie
        scoop.cmd install smoothie
        pause
        exit
    }
    2{
        CheckProcesses
        scoop.cmd update smoothie
        pause
        exit
    }
    3{
        CheckProcesses
        scoop.cmd uninstall smoothie
        pause
        exit
    }
}
}

if (-Not(Get-Command scoop.cmd -Ea Ignore)){
    Set-ExecutionPolicy Bypass -Scope Process -Force
    Invoke-RestMethod -Uri http://get.scoop.sh | Invoke-Expression
}

if (-Not(Get-Command git -Ea Ignore)){
    scoop.cmd install git
}

if ('utils' -NotIn (scoop.cmd bucket list)){
    scoop.cmd bucket add utils https://github.com/couleur-tweak-tips/utils
}

$IsFFmpegScoop = (Get-Command ffmpeg -Ea Ignore).Source -Like "*\shims\*"

if(Get-Command ffmpeg -Ea Ignore){

    $IsFFmpeg5 = (ffmpeg -hide_banner -h filter=libplacebo)

    if (-Not($IsFFmpeg5)){

        if ($IsFFmpegScoop){
            scoop.cmd update ffmpeg
        }else{
            Write-Warning @"
An FFmpeg installation was detected, but it is not version 5.0 or higher.
If you installed FFmpeg yourself, you can remove it and use the following command to install ffmpeg and add it to the path:

scoop.cmd install ffmpeg
"@
            
        }
        
    }
            
}else{
    scoop.cmd install ffmpeg
}

if ('utils' -NotIn (scoop.cmd bucket list)){
    Write-Warning "Failed to add the utils bucket"
    pause
    exit
}

scoop.cmd install utils/smoothie
