function Get-Path ($FileName){

    if (-Not(Get-Command $FileName -ErrorAction SilentlyContinue)){return $null}

    switch ($FileName.Split('.')[1]){
        'shim'{
            $Path = ((Get-Content ((Get-Command -ErrorAction SilentlyContinue).source)) -split ' = ')[1]
        }
        'exe'{

            $BaseName = $FileName.Split('.')[0]

            if (Get-Command "$BaseName.shim" -ErrorAction SilentlyContinue){

                $Path = ((Get-Content ((Get-Command "$BaseName.shim").source)) -split 'path = ')[1]

            }else{$Path = (Get-Command $FileName).source}
        }
    }

    if(-Not($Path)){$Path = (Get-Command $FileName).Source}

    return $Path

}

if (Get-Path sm){
    "@
A Smoothie installation has been detected, what would you like to do?

R - Reinstall smoothie (clean install)
U - Update smoothie to a newer version

@"
switch (choice.exe /C RU /N | Out-Null){
    1{
        $apps = @('smoothie','vapoursynth') # Sadly needs to be sequential 
        ForEach($app in $apps){scoop.cmd uninstall $app}
        ForEach($app in $apps){scoop.cmd install $app}
        exit
    }
    2{
        scoop.cmd update smoothie
        exit
    }
    3{
        "Would you also like to uninstall VapourSynth?"
        switch(choice /N | Out-Null){
            1{scoop.cmd uninstall vapoursynth}
        }
        scoop.cmd uninstall smoothie
        exit
    }
}
}

if (-Not(Get-Path git)){
    scoop.cmd install git
}

if ('utils' -NotIn (scoop.cmd bucket list)){
    scoop.cmd bucket add utils https://github.com/couleur-tweak-tips/utils
}

if (Get-Path vspipe){
    "VapourSynth installation detected, installation is likely to fail if you have it set up incorrectly"
}else{
    scoop.cmd install vapoursynth
}

$installed = vsrepo installed

@(
    'ffms2'
    'havsfunc'
)

if(Get-Path ffmpeg){
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
