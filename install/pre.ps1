if (Get-Command sm  -Ea Ignore){
"@
A Smoothie installation has been detected, what would you like to do?

Press R to Reinstall Smoothie (clean install)
Press V to update Smoothie to a newer Version
Press U to uninstall Smoothie

@"
switch (choice.exe /C RVU /N | Out-Null){
    1{
        scoop.cmd uninstall smoothie
        scoop.cmd install smoothie
        pause
        exit
    }
    2{
        scoop.cmd update smoothie
        pause
        exit
    }
    3{
        scoop.cmd uninstall smoothie
        pause
        exit
    }
}
}

if (-Not(Get-Command scoop.cmd -Ea Ignore)){
    Invoke-RestMethod -Uri http://get.scoop.sh | Invoke-Expression
}

if (-Not(Get-Command git -Ea Ignore)){
    scoop.cmd install git
}

if ('utils' -NotIn (scoop.cmd bucket list)){
    scoop.cmd bucket add utils https://github.com/couleur-tweak-tips/utils
}

if(Get-Command ffmpeg -Ea Ignore){
    if ((ffmpeg -hide_banner -h filter=libplacebo) -eq "Unknown filter 'libplacebo'."){ # Check if libplacebo is installed, therefore if ffmpeg is atleast version 5.0 s/o vlad
        if ((Get-Command ffmpeg).Source -like "*\shims\*"){
            scoop.cmd update ffmpeg
        }else{
            scoop.cmd install ffmpeg
        }
    }
}else{
    scoop.cmd install ffmpeg
}

scoop.cmd install utils/smoothie
