$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$outDir = Join-Path $PWD "out"

if (Test-Path $outDir) {
    Remove-Item -Path $outDir -Recurse -Force
}

$vapoursynthDir = Join-Path $outDir "vapoursynth"
$ffmpegDir = Join-Path $outDir "ffmpeg"
$pluginsDir = Join-Path $vapoursynthDir "vs-plugins"
$modelsBaseDir = Join-Path $outDir "models"

# Create directories if they don't exist
New-Item -ItemType Directory -Force -Path $vapoursynthDir | Out-Null
New-Item -ItemType Directory -Force -Path $ffmpegDir | Out-Null
New-Item -ItemType Directory -Force -Path $pluginsDir | Out-Null
New-Item -ItemType Directory -Force -Path $modelsBaseDir | Out-Null

# Function to download file
function Download-File {
    param (
        [string]$Url,
        [string]$OutFile,
        [string]$Sha256 # only needed when the url could change
    )

    Write-Host "Downloading $Url to $OutFile"
    $webClient = New-Object System.Net.WebClient
    $webClient.DownloadFile($Url, $OutFile)

    if ($Sha256) {
        $hash = (Get-FileHash $OutFile -Algorithm SHA256).Hash
        if ($hash -ne $Sha256) {
            Remove-Item $OutFile
            throw "$Url has SHA256 $hash, expected $Sha256"
        }
    }

    Write-Host "Download completed"
}

# Function to extract files from archive and copy to destination
function Extract-Files {
    param (
        [string]$ArchivePath,
        [string[]]$FilePatterns,
        [string]$DestinationPath
    )

    if (-not (Get-Command "7z" -ErrorAction SilentlyContinue)) {
        throw "7z not found. Please install 7-Zip and make sure it's in your PATH."
    }

    $tempDir = [System.IO.Path]::Combine([System.IO.Path]::GetDirectoryName($ArchivePath), "temp-extract-" + [System.IO.Path]::GetRandomFileName())
    New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

    try {
        # Extract archive
        & 7z x $ArchivePath -o"$tempDir" -y

        foreach ($pattern in $FilePatterns) {
            $filePath = Join-Path $tempDir $pattern
            if (Test-Path $filePath) {
                if (Test-Path $filePath -PathType Container) {
                    Copy-Item -Path $filePath -Destination $DestinationPath -Recurse
                }
                else {
                    Copy-Item -Path $filePath -Destination $DestinationPath
                }
                Write-Host "Copied $pattern to $DestinationPath"
            }
            else {
                throw "Could not find $pattern in $ArchivePath"
            }
        }
    }
    finally {
        # Clean up
        if (Test-Path $tempDir) {
            Remove-Item -Path $tempDir -Recurse -Force
        }
        if (Test-Path $ArchivePath) {
            Remove-Item -Path $ArchivePath -Force
        }
    }
}

# Function to download model files from GitHub
function Download-ModelFiles {
    param (
        [string]$BaseUrl,
        [string]$ModelName,
        [string[]]$FileList
    )

    Write-Host "Downloading model: $ModelName"
    $modelDir = Join-Path $modelsBaseDir $ModelName

    # Create model directory if it doesn't exist
    if (-not (Test-Path $modelDir)) {
        New-Item -ItemType Directory -Force -Path $modelDir | Out-Null
        Write-Host "Created directory: $modelDir"
    }

    foreach ($file in $FileList) {
        $fileUrl = "$BaseUrl/$file"
        $outputPath = Join-Path $modelDir $file
        Download-File -Url $fileUrl -OutFile $outputPath
    }

    Write-Host "Model $ModelName download completed"
}

# Download and install VapourSynth
$vapoursynthInstallerArchive = Join-Path $vapoursynthDir "vapoursynth-installer.zip"
Download-File -Url "https://github.com/vapoursynth/vapoursynth/releases/download/R79/Install-Portable-VapourSynth-R79.zip" -OutFile $vapoursynthInstallerArchive
Extract-Files -ArchivePath $vapoursynthInstallerArchive -FilePatterns @("Install-Portable-VapourSynth-R79.ps1") -DestinationPath $vapoursynthDir
$vapoursynthInstallerPath = Join-Path $vapoursynthDir "Install-Portable-VapourSynth-R79.ps1"

# Run VapourSynth installer
Write-Host "Running VapourSynth installer..."
Push-Location $vapoursynthDir
& $vapoursynthInstallerPath -Unattended -TargetFolder $vapoursynthDir

# the blur scripts need numpy. plugins on pypi install into vapoursynth's own plugins folder, which it autoloads
Write-Host "Installing numpy and plugins into VapourSynth's Python..."
& (Join-Path $vapoursynthDir "python.exe") -m pip install --no-warn-script-location `
    numpy==2.5.3 `
    vapoursynth-akarin==1.5.0 `
    vapoursynth-bestsource==21.0 `
    vapoursynth-fmtconv==31 `
    vapoursynth-lsmas==1310.0.0.0 `
    vapoursynth-mvtools==29
& (Join-Path $vapoursynthDir "python.exe") -m pip uninstall --yes pip

Write-Host "Cleaning up VapourSynth"
Remove-Item -Path doc -Recurse -Force
Remove-Item -Path Scripts -Recurse -Force
Remove-Item -Path vs-temp-dl -Recurse -Force
Remove-Item -Path wheel -Recurse -Force
Remove-Item -Path pip.bat
Remove-Item -Path $vapoursynthInstallerPath
Remove-Item -Path Lib\site-packages\vapoursynth\*.pdb

Pop-Location

# the installer installs this if the msvc runtime is missing or older. it has to be at least as new as the newest
# toolset any bundled binary was built with
$redistDir = Join-Path $outDir "redist"
New-Item -ItemType Directory -Force -Path $redistDir | Out-Null
Download-File -Url "https://download.visualstudio.microsoft.com/download/pr/ebdab8e5-1d7b-4d9f-a11b-cbb1720c3b12/843068991DAAA1F73AD9F6239BCE4D0F6A07A51F18C37EA2A867E9BECA71295C/VC_redist.x64.exe" -OutFile (Join-Path $redistDir "vc_redist.x64.exe") -Sha256 "843068991DAAA1F73AD9F6239BCE4D0F6A07A51F18C37EA2A867E9BECA71295C"

# the installer extracts tensorrt with it
$sevenZipDir = Join-Path $outDir "7zip"
New-Item -ItemType Directory -Force -Path $sevenZipDir | Out-Null
$sevenZipArchive = Join-Path $sevenZipDir "7zip.exe"
Download-File -Url "https://github.com/ip7z/7zip/releases/download/26.03/7z2603-x64.exe" -OutFile $sevenZipArchive -Sha256 "0859C524B8A63551848F0C246ABDDCB1D0B7B656B0FBFE879F8D85E61A9E6EDD"
Extract-Files -ArchivePath $sevenZipArchive -FilePatterns @("7z.exe", "7z.dll") -DestinationPath $sevenZipDir

# plugins that aren't on pypi
$plugins = @(
    @{
        Name        = "FrameBlender";
        Url         = "https://github.com/f0e/vs-frameblender/releases/download/v2.1/frameblender-windows-x64.dll";
        Sha256      = "eab7f45949109513bd7bec3f71111d219e28c9f1ebb22aa13805815a7d8a629a";
        IsDirectDll = $true;
    },
    @{
        Name        = "VapourSynth-RIFE-ncnn-Vulkan";
        Url         = "https://github.com/styler00dollar/VapourSynth-RIFE-ncnn-Vulkan/releases/download/r9_mod_v33/librife_windows_x86-64.dll";
        Sha256      = "36a25b471be88e6f915320c818022dc8657dd9beac22a8c3158bd7f4260cc410";
        IsDirectDll = $true;
    },
    @{
        Name         = "SVPFlow";
        Url          = "https://web.archive.org/web/20190322064557if_/http://www.svp-team.com/files/gpl/svpflow-4.2.0.142.zip";
        FilePatterns = @(
            "svpflow-4.2.0.142/lib-windows/vapoursynth/x64/svpflow1_vs64.dll",
            "svpflow-4.2.0.142/lib-windows/vapoursynth/x64/svpflow2_vs64.dll"
        );
    },
    @{
        Name         = "AVISource";
        Url          = "https://github.com/vapoursynth/vs-avisource-obsolete/releases/download/R1/avisource-r1.7z";
        Sha256       = "f43b6285b90fee682d9f669fe6aedca22592b7ba9d4be77ea5965a0af908d124";
        FilePatterns = @("win64/avisource.dll");
    }
)

foreach ($plugin in $plugins) {
    Write-Host "Processing $($plugin.Name) plugin..."

    # Determine if it's a direct DLL download by checking if IsDirectDll is true
    $isDirectDll = $plugin.ContainsKey('IsDirectDll') -and $plugin.IsDirectDll

    if ($isDirectDll) {
        # Direct DLL download (no extraction needed)
        $dllPath = Join-Path $pluginsDir "$($plugin.Name.ToLower()).dll"
        Download-File -Url $plugin.Url -OutFile $dllPath -Sha256 $plugin.Sha256
    }
    else {
        # Archive download that needs extraction
        $archiveExt = if ($plugin.Url.EndsWith('.zip') -or $plugin.Url.EndsWith('.whl')) { '.zip' } else { '.7z' }
        $archivePath = Join-Path $vapoursynthDir "$($plugin.Name.ToLower())$archiveExt"
        Download-File -Url $plugin.Url -OutFile $archivePath -Sha256 $plugin.Sha256
        Extract-Files -ArchivePath $archivePath -FilePatterns $plugin.FilePatterns -DestinationPath $pluginsDir
    }
}

# Download and process FFmpeg
# the shared build, so ffmpeg and ffprobe share the libraries instead of each having a copy
$ffmpegUrl = "https://github.com/GyanD/codexffmpeg/releases/download/8.0.1/ffmpeg-8.0.1-full_build-shared.7z"
$ffmpegArchive = Join-Path $ffmpegDir "ffmpeg.7z"
Download-File -Url $ffmpegUrl -OutFile $ffmpegArchive -Sha256 "8030dc469fbde247b84cfc21a5c421f3965ffe779bc35de08d78966e0c4a272c"
Extract-Files -ArchivePath $ffmpegArchive -FilePatterns @(
    "ffmpeg-8.0.1-full_build-shared\bin\ffmpeg.exe",
    "ffmpeg-8.0.1-full_build-shared\bin\ffprobe.exe",
    "ffmpeg-8.0.1-full_build-shared\bin\*.dll"
) -DestinationPath $ffmpegDir

# Define model downloads
$modelDownloads = @(
    @{
        BaseUrl   = "https://raw.githubusercontent.com/styler00dollar/VapourSynth-RIFE-ncnn-Vulkan/96fdcecdf982ef3237e16e2b105eea849fa022ab/models/rife-v4.26_ensembleFalse";
        ModelName = "rife-v4.26_ensembleFalse";
        FileList  = @("flownet.bin", "flownet.param");
    }
)

# Download all models
foreach ($model in $modelDownloads) {
    Download-ModelFiles -BaseUrl $model.BaseUrl -ModelName $model.ModelName -FileList $model.FileList
}
