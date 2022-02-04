if (Get-Command sm  -Ea Ignore){
    "@
A Smoothie installation has been detected, what would you like to do?

R - Reinstall smoothie (clean install)
U - Update smoothie to a newer version

@"
switch (choice.exe /C RU /N | Out-Null){
    1{
        scoop.cmd uninstall smoothie
        scoop.cmd install smoothie
        exit
    }
    2{
        scoop.cmd update smoothie
        exit
    }
    3{
        scoop.cmd uninstall smoothie
    }
}
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
