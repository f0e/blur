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
$vapoursynthInstallerUrl = "https://github.com/vapoursynth/vapoursynth/releases/download/R72/Install-Portable-VapourSynth-R72.ps1"
$vapoursynthInstallerPath = Join-Path $vapoursynthDir "Install-Portable-VapourSynth-R72.ps1"
Download-File -Url $vapoursynthInstallerUrl -OutFile $vapoursynthInstallerPath

# Run VapourSynth installer
Write-Host "Running VapourSynth installer..."
Push-Location $vapoursynthDir
& $vapoursynthInstallerPath -Unattended -TargetFolder $vapoursynthDir

# the blur scripts need numpy
Write-Host "Installing numpy into VapourSynth's Python..."
& (Join-Path $vapoursynthDir "python.exe") -m pip install --no-warn-script-location numpy==2.5.3
& (Join-Path $vapoursynthDir "python.exe") -m pip uninstall --yes pip

Write-Host "Cleaning up VapourSynth"
Remove-Item -Path doc -Recurse -Force
Remove-Item -Path Scripts -Recurse -Force
Remove-Item -Path vs-temp-dl -Recurse -Force
Remove-Item -Path $vapoursynthInstallerPath

# avfs and the pismo file mount installer it needs aren't used
Remove-Item -Path AVFS.exe
Remove-Item -Path pfm-*.exe

Pop-Location

# Plugin installations
$plugins = @(
    @{
        Name         = "Akarin";
        Url          = "https://files.pythonhosted.org/packages/0d/31/95658c029a7ee3bbfc1359f9fa623a13f0a3ff0d940ba9e204421dd7a0ca/vapoursynth_akarin-1.5.0-py3-none-win_amd64.whl";
        FilePatterns = @(
            "vapoursynth/plugins/akarin/libakarin.dll",
            "vapoursynth/plugins/akarin/libzstd.dll" # akarin links against this
        );
    },
    @{
        Name        = "FrameBlender";
        Url         = "https://github.com/f0e/vs-frameblender/releases/download/v2/frameblender-windows-x64.dll";
        Sha256      = "5e85104ade54eee4875b2f0271e755e511778b7329f7cd68cfc027b444288708";
        IsDirectDll = $true;
    },
    @{
        Name         = "BestSource";
        Url          = "https://github.com/vapoursynth/bestsource/releases/download/R11/BestSource-R11.7z";
        Sha256       = "d6c88a0b5f6a72d80602a59f5b365dae6ad2d2ad48e7b876a1392e005f5387c0";
        FilePatterns = @("BestSource.dll");
    },
    @{
        Name         = "LSmashSource";
        Url          = "https://files.pythonhosted.org/packages/a2/4f/f5804dbeb6563486e1d72ce3e72cb9ae2c7201c6660112447f2e1c6f85ec/vapoursynth_lsmas-1310.0.0.0-py3-none-win_amd64.whl";
        FilePatterns = @("vapoursynth/plugins/LSMASHSource.dll");
    },
    @{
        Name         = "MVTools";
        Url          = "https://github.com/dubhater/vapoursynth-mvtools/releases/download/v24/vapoursynth-mvtools-v24-win64.7z";
        Sha256       = "b9883003eed100d4ffd44344b658554076b89da6d45041b378e675720426bc95";
        FilePatterns = @("libmvtools.dll");
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
        Name         = "FmtConv";
        Url          = "https://ldesoras.fr/src/vs/fmtconv-r31.zip";
        Sha256       = "09038091bc5b1f587f6464ed6324f57b667c09fa65c6fcd6bb86e75da83365e0";
        FilePatterns = @("win64/fmtconv.dll");
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
