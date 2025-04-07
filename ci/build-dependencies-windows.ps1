# Create necessary directories
$outDir = Join-Path $PWD "out"
$vapoursynthDir = Join-Path $outDir "vapoursynth"
$ffmpegDir = Join-Path $outDir "ffmpeg"
$pluginsDir = Join-Path $vapoursynthDir "vs-plugins"

# Create directories if they don't exist
New-Item -ItemType Directory -Force -Path $vapoursynthDir | Out-Null
New-Item -ItemType Directory -Force -Path $ffmpegDir | Out-Null
New-Item -ItemType Directory -Force -Path $pluginsDir | Out-Null

# Function to download file
function Download-File {
    param (
        [string]$Url,
        [string]$OutFile
    )
    
    Write-Host "Downloading $Url to $OutFile"
    $webClient = New-Object System.Net.WebClient
    $webClient.DownloadFile($Url, $OutFile)
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
        Write-Warning "7z not found. Please install 7-Zip and make sure it's in your PATH."
        Write-Warning "7z file is located at: $ArchivePath"
        return
    }

    $tempDir = [System.IO.Path]::Combine([System.IO.Path]::GetDirectoryName($ArchivePath), "temp-extract-" + [System.IO.Path]::GetRandomFileName())
    New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
    
    try {
        # Extract archive
        & 7z x $ArchivePath -o"$tempDir" -y
        
        foreach ($pattern in $FilePatterns) {
            $filePath = Join-Path $tempDir $pattern
            if (Test-Path $filePath) {
                Copy-Item -Path $filePath -Destination $DestinationPath
                Write-Host "Copied $pattern to $DestinationPath"
            } else {
                Write-Warning "Could not find $pattern in the extracted files."
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

# Download and install VapourSynth
$vapoursynthInstallerUrl = "https://github.com/vapoursynth/vapoursynth/releases/download/R70/Install-Portable-VapourSynth-R70.ps1"
$vapoursynthInstallerPath = Join-Path $vapoursynthDir "Install-Portable-VapourSynth-R70.ps1"
Download-File -Url $vapoursynthInstallerUrl -OutFile $vapoursynthInstallerPath

# Run VapourSynth installer
Write-Host "Running VapourSynth installer..."
Set-Location $vapoursynthDir
& $vapoursynthInstallerPath -Unattended -TargetFolder $vapoursynthDir

Write-Host "Cleaning up VapourSynth"
Remove-Item -Path doc -Recurse -Force
Remove-Item -Path Scripts -Recurse -Force
Remove-Item -Path vs-temp-dl -Recurse -Force
Remove-Item -Path $vapoursynthInstallerPath

Set-Location $PWD

# Plugin installations
$plugins = @(
    @{
        Name = "Akarin";
        Url = "https://github.com/AkarinVS/vapoursynth-plugin/releases/download/v0.96/akarin-release-lexpr-amd64-v0.96g3.7z";
        FilePatterns = @("akarin.dll");
    },
    @{
        Name = "BestSource";
        Url = "https://github.com/vapoursynth/bestsource/releases/download/R11/BestSource-R11.7z";
        FilePatterns = @("BestSource.dll");
    },
    @{
        Name = "MVTools";
        Url = "https://github.com/dubhater/vapoursynth-mvtools/releases/download/v24/vapoursynth-mvtools-v24-win64.7z";
        FilePatterns = @("libmvtools.dll");
    },
    @{
        Name = "SVPFlow";
        Url = "https://web.archive.org/web/20190322064557if_/http://www.svp-team.com/files/gpl/svpflow-4.2.0.142.zip";
        FilePatterns = @(
            "svpflow-4.2.0.142/lib-windows/vapoursynth/x64/svpflow1_vs64.dll",
            "svpflow-4.2.0.142/lib-windows/vapoursynth/x64/svpflow2_vs64.dll"
        );
    }
)

foreach ($plugin in $plugins) {
    Write-Host "Processing $($plugin.Name) plugin..."
    $archivePath = Join-Path $vapoursynthDir "$($plugin.Name.ToLower())$(if ($plugin.Name -eq 'SVPFlow') {'.zip'} else {'.7z'})"
    Download-File -Url $plugin.Url -OutFile $archivePath
    Extract-Files -ArchivePath $archivePath -FilePatterns $plugin.FilePatterns -DestinationPath $pluginsDir
}

# Download and process FFmpeg
$ffmpegUrl = "https://www.gyan.dev/ffmpeg/builds/ffmpeg-git-essentials.7z"
$ffmpegArchive = Join-Path $ffmpegDir "ffmpeg-git-essentials.7z"
Download-File -Url $ffmpegUrl -OutFile $ffmpegArchive
Extract-Files -ArchivePath $ffmpegArchive -FilePatterns @("ffmpeg-2025-03-31-git-35c091f4b7-essentials_build\bin\ffmpeg.exe") -DestinationPath $ffmpegDir

Write-Host "Done"