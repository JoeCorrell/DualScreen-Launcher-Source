@echo off
setlocal EnableExtensions

set "SETUP_ROOT=%~dp0"
set "REDIST=%SETUP_ROOT%redist"
set "STATE=%LOCALAPPDATA%\DualScreen Launcher\TransformersWFC"
set "RECEIPT=%STATE%\prerequisites-v2.complete"
set "FAILURE=%STATE%\prerequisites-v2.failed"

call :verify_receipt
if not errorlevel 1 goto launch_game

echo [DualScreen] Preparing Transformers: War for Cybertron prerequisites...
if not exist "%STATE%" mkdir "%STATE%"
if not exist "%STATE%" (
    echo [DualScreen] Could not create the prerequisite state directory. 1>&2
    exit /b 24
)
if exist "%FAILURE%" del /f /q "%FAILURE%" >nul 2>&1
if not exist "%REDIST%\PhysX_10.01.29_9.10.0129_SystemSoftware.exe" goto missing_payload
if not exist "%REDIST%\vcredist_x86_2005sp1\vcredist_x86.exe" goto missing_payload
if not exist "%REDIST%\DirectX\DXSETUP.exe" goto missing_payload

if exist "%RECEIPT%" del /f /q "%RECEIPT%" >nul 2>&1

echo [DualScreen] Installing NVIDIA PhysX 9.10.0129...
start "" /wait "%REDIST%\PhysX_10.01.29_9.10.0129_SystemSoftware.exe" /quiet
call :require_success PhysX %errorlevel%
if errorlevel 1 (
    call :record_failure 20 PhysX
    exit /b 20
)

echo [DualScreen] Installing Microsoft Visual C++ 2005 SP1 x86...
start "" /wait "%REDIST%\vcredist_x86_2005sp1\vcredist_x86.exe" /q
call :require_success "Visual C++ 2005 SP1 x86" %errorlevel%
if errorlevel 1 (
    call :record_failure 21 "Visual C++ 2005 SP1 x86"
    exit /b 21
)

echo [DualScreen] Verifying the WEMU native Microsoft DirectX legacy runtime...
rem DXSETUP is retained in the independent payload, but its ARM32 rundll32 helper
rem cannot complete under this game's required Box64/ARM64EC runtime. WEMU has
rem already installed the same DirectX, XAudio, and XACT DLL sets natively.
call :verify_directx
if errorlevel 1 (
    echo [DualScreen] The native DirectX, XAudio, and XACT components are incomplete. 1>&2
    call :record_failure 22 DirectX
    exit /b 22
)
echo [DualScreen] Microsoft DirectX, XAudio, and XACT components are ready.

call :verify_files
if errorlevel 1 (
    echo [DualScreen] Prerequisite installers completed but verification failed. 1>&2
    call :record_failure 23 verification
    exit /b 23
)

>"%RECEIPT%" echo WFC_PREREQUISITES_V2
call :verify_receipt
if errorlevel 1 (
    echo [DualScreen] Could not persist prerequisite completion state. 1>&2
    call :record_failure 25 receipt
    exit /b 25
)

:launch_game
cd /d "%SETUP_ROOT%.."
start "" /wait "TWFC.exe"
exit /b 0

:verify_receipt
if not exist "%RECEIPT%" exit /b 1
findstr /x /c:"WFC_PREREQUISITES_V2" "%RECEIPT%" >nul 2>&1
if errorlevel 1 exit /b 1
call :verify_files
exit /b %errorlevel%

:verify_files
call :verify_directx
if errorlevel 1 exit /b 1
dir /b /s "%WINDIR%\msvcr80.dll" >nul 2>&1
if errorlevel 1 exit /b 1
if exist "%ProgramFiles%\NVIDIA Corporation\PhysX\Common\PhysXLoader.dll" exit /b 0
if exist "%ProgramFiles(x86)%\NVIDIA Corporation\PhysX\Common\PhysXLoader.dll" exit /b 0
exit /b 1

:verify_directx
if not exist "%WINDIR%\system32\d3dx9_42.dll" exit /b 1
if not exist "%WINDIR%\syswow64\d3dx9_42.dll" exit /b 1
if not exist "%WINDIR%\system32\xactengine3_0.dll" exit /b 1
if not exist "%WINDIR%\syswow64\xactengine3_0.dll" exit /b 1
if not exist "%WINDIR%\system32\xaudio2_7.dll" exit /b 1
if not exist "%WINDIR%\syswow64\xaudio2_7.dll" exit /b 1
exit /b 0

:require_success
set "COMPONENT=%~1"
set "STATUS=%~2"
if "%STATUS%"=="0" exit /b 0
if "%STATUS%"=="3010" exit /b 0
if "%STATUS%"=="1638" exit /b 0
echo [DualScreen] %COMPONENT% installer failed with status %STATUS%. 1>&2
exit /b 1

:missing_payload
echo [DualScreen] The verified prerequisite payload is incomplete. 1>&2
call :record_failure 19 payload
exit /b 19

:record_failure
>"%FAILURE%" echo status=%~1 component=%~2
exit /b 0
