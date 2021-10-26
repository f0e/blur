@echo off

:: Script that installs all the required dependencies for tekno's blur.
:: - couleur / modified by tekno

title Blur dependencies installer

:: -------------------------- Administrator access request --------------------------

:: https://privacy.sexy — v0.10.2 — Tue, 20 Jul 2021 11:05:44 GMT
:: Ensure admin privileges
fltmc >nul 2>&1 || (
    echo Administrator privileges are required.
    PowerShell Start -Verb RunAs '%0' 2> nul || (
        echo Right-click on the script and select "Run as administrator".
        pause & exit 1
    )
    exit 0
)

:: ------------------------------- Python installation ------------------------------

:python
cls
echo 2. Checking for Python...

:: Check if Python is installed
python --version 2>NUL
if %ERRORLEVEL% equ 0 (
    echo 2. Python found!
    goto ffmpeg
)


echo.

SET /P use_chocolatey="Python not found. Install using Chocolatey? (alternatively, download and run the installer) (y/n) "
if /I "%use_chocolatey%" == "y" (goto python_choco)
if /I not "%use_chocolatey%" == "y" (goto python_manual)

:python_choco
cls
echo 2.1. Checking for Chocolatey...

:: Check if Chocolatey is installed
where choco>nul 2>nul
if %ERRORLEVEL% equ 0 (goto python_choco_2)

echo Chocolatey not found. Installing...
echo.

:: Install Chocolatey
echo Installing Chocolatey...
powershell Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

pause

:python_choco_2
cls
echo 2.2. Installing Python through Chocolatey...

choco install python -y

pause
goto ffmpeg

:python_manual

:: Download installer
powershell Invoke-WebRequest "https://www.python.org/ftp/python/3.9.6/python-3.9.6-amd64.exe" -OutFile "%temp%\python-installer.exe"

:: Run installer
start /wait %temp%\python-installer.exe

:: Delete installer
del %temp%\python-installer.exe

pause

:: ------------------------------- FFmpeg installation ------------------------------

:ffmpeg
cls
echo 2. Checking for FFmpeg...

:: Check if FFmpeg is installed
where ffmpeg>nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo 2. FFmpeg found!
    goto vapoursynth
)

echo.

SET /P use_chocolatey="FFmpeg not found. Install automatically using Chocolatey? (y/n) "
if /I "%use_chocolatey%" == "y" (goto ffmpeg_choco)
if /I not "%use_chocolatey%" == "y" (goto ffmpeg_manual)

:ffmpeg_choco
cls
echo 2.1. Checking for Chocolatey...

:: Check if Chocolatey is installed
where choco>nul 2>nul
if %ERRORLEVEL% equ 0 (goto ffmpeg_choco_2)

echo Chocolatey not found. Installing...
echo.

:: Install Chocolatey
echo Installing Chocolatey...
powershell Set-ExecutionPoicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))

pause

:ffmpeg_choco_2
cls
echo 2.2. Installing FFmpeg through Chocolatey...

choco install ffmpeg -y

pause
goto vapoursynth

:ffmpeg_manual
cls
echo FFmpeg is required to run Blur.
echo You can close this window, or press any key to get redirected to a guide on manually installing FFmpeg.
echo.
pause
start https://www.wikihow.com/Install-FFmpeg-on-Windows
exit

:: ---------------------------- VapourSynth installation ----------------------------

:vapoursynth
cls
echo 3. Checking for VapourSynth...

:: Check if VapourSynth is installed
where vspipe>nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo 2. VapourSynth found!
    goto vapoursynth_plugins
)

echo VapourSynth not found. Downloading installer...
echo.

:: Download installer
powershell Invoke-WebRequest "https://github.com/vapoursynth/vapoursynth/releases/download/R54/VapourSynth64-R54.exe" -OutFile "%temp%\VapourSynth-installer.exe"

:: Run installer
start /wait %temp%\VapourSynth-installer.exe

:: Delete installer
del %temp%\VapourSynth-installer.exe

pause

:: ------------------------ VapourSynth plugins installation ------------------------

:vapoursynth_plugins
cls
echo 4. Installing VapourSynth plugins...
echo.

vsrepo.py -d install ffms2 havsfunc mvsfunc adjust mvtools

:svpflow
cls
echo 4.1. Installing SVPflow
echo.

if exist "%homepath%\AppData\Roaming\VapourSynth\plugins64" (
    set plugins_folder="%homepath%\AppData\Roaming\VapourSynth\plugins64"
) else (
    if exist "%PROGRAMFILES(x86)%\VapourSynth\plugins64" (
        set plugins_folder="%PROGRAMFILES(x86)%\VapourSynth\plugins64"
    ) else (
        echo Couldn't find VapourSynth plugins folder, try manually installing SVPflow
        echo.
        pause
        exit
    )
)

set plugins_folder=%plugins_folder:"=%

if exist "%plugins_folder%\svpflow1_vs64.dll" (
    if exist "%plugins_folder%\svpflow2_vs64.dll" (
        goto frameblender
    )
)

powershell Invoke-WebRequest "https://cdn.discordapp.com/attachments/588965578493001729/861296635697823754/svpflow1_vs64.dll" -OutFile "%temp%\svpflow1_vs64.dll"
powershell Invoke-WebRequest "https://cdn.discordapp.com/attachments/588965578493001729/861296631924129862/svpflow2_vs64.dll" -OutFile "%temp%\svpflow2_vs64.dll"

move %temp%\svpflow1_vs64.dll "%plugins_folder%\svpflow1_vs64.dll">nul
move %temp%\svpflow2_vs64.dll "%plugins_folder%\svpflow2_vs64.dll">nul

pause

:frameblender
cls
echo 4.2. Installing vs-frameblender
echo.

if exist "%plugins_folder%\vs-frameblender-x64.dll" (
    goto weighting
)

powershell Invoke-WebRequest "https://github.com/f0e/vs-frameblender/releases/latest/download/vs-frameblender-x64.dll" -OutFile "%temp%\vs-frameblender-x64.dll"

move %temp%\vs-frameblender-x64.dll "%plugins_folder%\vs-frameblender-x64.dll">nul

pause

:weighting
cls
echo 4.3. Installing weighting.py
echo.

if exist "%homepath%\AppData\Roaming\Python\Python39\site-packages" (
    set python_packages_folder="%homepath%\AppData\Roaming\Python\Python39\site-packages"
) else (
    echo Couldn't find Python's site-packages folder, try manually installing weighting.py
    echo.
    pause
    exit
)

set python_packages_folder=%python_packages_folder:"=%

if exist "%python_packages_folder%\weighting.py" (
    goto filldrops
)

powershell Invoke-WebRequest "https://raw.githubusercontent.com/f0e/blur/master/plugins/weighting.py" -OutFile "%temp%\weighting.py"

move %temp%\weighting.py "%python_packages_folder%\weighting.py">nul

pause

:filldrops
cls
echo 4.4. Installing filldrops.py
echo.

set python_packages_folder=%python_packages_folder:"=%

if exist "%python_packages_folder%\filldrops.py" (
    goto numpy
)

powershell Invoke-WebRequest "https://raw.githubusercontent.com/f0e/blur/master/plugins/filldrops.py" -OutFile "%temp%\filldrops.py"

move %temp%\filldrops.py "%python_packages_folder%\filldrops.py">nul

pause

:numpy
cls
echo 5. Installing numpy...
echo.

pip install numpy

:end
cls
echo Dependencies fully installed!
pause
