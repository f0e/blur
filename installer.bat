:: Script that installs all the required dependencies for tekno's blur.
:: - couleur / modified by tekno

@echo off

title Blur dependencies installer

:: -------------------------- Administrator access request --------------------------

:check_permissions
    net session >nul 2>&1
    if %errorLevel% == 0 (
        goto got_admin
    ) else (
        goto request_administrator
        pause
        exit
    )

:request_administrator
    echo Set UAC = CreateObject^("Shell.Application"^) > "%temp%\getadmin.vbs"
    echo UAC.ShellExecute "%~s0", "", "", "runas", 1 >> "%temp%\getadmin.vbs"

    "%temp%\getadmin.vbs"
    exit /B

:got_admin
    if exist "%temp%\getadmin.vbs" ( del "%temp%\getadmin.vbs" )
    pushd "%CD%"
    CD /D "%~dp0"

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
powershell Set-ExecutionPoicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))

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
powershell Invoke-WebRequest "https://github.com/vapoursynth/vapoursynth/releases/download/R53/VapourSynth64-R53.exe" -OutFile "%temp%\VapourSynth-installer.exe"

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

vsrepo.py install ffms2 havsfunc

pause

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
        goto end
    )
)

powershell Invoke-WebRequest "https://cdn.discordapp.com/attachments/588965578493001729/861296635697823754/svpflow1_vs64.dll" -OutFile "%temp%\svpflow1_vs64.dll"
powershell Invoke-WebRequest "https://cdn.discordapp.com/attachments/588965578493001729/861296631924129862/svpflow2_vs64.dll" -OutFile "%temp%\svpflow2_vs64.dll"

move %temp%\svpflow1_vs64.dll "%plugins_folder%\svpflow1_vs64.dll">nul
move %temp%\svpflow2_vs64.dll "%plugins_folder%\svpflow2_vs64.dll">nul

pause

:end
cls
echo Dependencies fully installed!
pause
