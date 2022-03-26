Set-ExecutionPolicy Bypass -Scope Process -Force # Required for scoop

function CheckProcesses{
    if (Get-Process -Name python,sm,vspipe,ffmpeg -ErrorAction Ignore){
        Write-Warning @"
The following processes are still running and may cause uninstallation issues

make sure these processes are not directly related to Smoothie before continuing:
"@
        Get-Process -Name python,sm,vspipe,ffmpeg -ErrorAction Ignore | Select-Object ProcessName, Path
        pause
    }     

}

if (Get-Command sm -Ea Ignore){
    Write-Output @"

A Smoothie installation has been detected, what would you like to do?

Press R to Reinstall Smoothie (clean install)
Press V to update Smoothie to a newer version
Press U to uninstall Smoothie
Press Q to quit
"@
    $Choice = choice.exe /C RVUQ /N
    switch ($Choice){
        'R'{
            CheckProcesses
            scoop uninstall smoothie
            scoop install smoothie
            pause
            exit
        }
        'V'{
            CheckProcesses
            scoop update smoothie
            pause
            exit
        }
        'U'{
            CheckProcesses
            scoop uninstall smoothie
            pause
            exit
        }
        'Q'{
            exit
        }
    }
}

if (-Not(Get-Command scoop -Ea Ignore)){

    $RunningAsAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]'Administrator')

    If (-Not($RunningAsAdmin)){
        Invoke-Expression (Invoke-WebRequest -UseBasicParsing -Uri http://get.scoop.sh)
    }else{
        Invoke-Expression "& {$(Invoke-WebRequest -UseBasicParsing -Uri https://get.scoop.sh)} -RunAsAdmin"
    }
}

if (-Not(Get-Command git -Ea Ignore)){
    scoop install git
}

if ('utils' -NotIn ((scoop bucket list).Name)){
    scoop bucket add utils https://github.com/couleur-tweak-tips/utils
}

$IsFFmpegScoop = (Get-Command ffmpeg -Ea Ignore).Source -Like "*\shims\*"

if(Get-Command ffmpeg -Ea Ignore){

    $IsFFmpeg5 = (ffmpeg -hide_banner -h filter=libplacebo)

    if (-Not($IsFFmpeg5)){

        if ($IsFFmpegScoop){
            scoop update ffmpeg
        }else{
            Write-Warning @"
An FFmpeg installation was detected, but it is not version 5.0 or higher.
If you installed FFmpeg yourself, you can remove it and use the following command to install ffmpeg and add it to the path:

scoop install ffmpeg
"@
            
        }
        
    }
            
}else{
    scoop install ffmpeg
}

if ('utils' -NotIn (scoop bucket list).Name){
    Write-Warning "Failed to add the utils bucket"
    pause
    exit
}

scoop install utils/smoothie
