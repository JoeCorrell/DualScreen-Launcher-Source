@echo off
setlocal
rem Diagnostic only. No registry writes, DEP changes, DLL overrides or game patches.
set "CRYSIS_EXE=F:\Download\Crysis3\Crysis 3\Bin32\Crysis3.exe"
set "CRYSIS_DIR=F:\Download\Crysis3\Crysis 3\Bin32"
if not exist "%CRYSIS_EXE%" goto missing
cd /d "%CRYSIS_DIR%"
if errorlevel 1 goto missing
:newlog
set "CRYSIS_LOG=%~dp0crysis3-%RANDOM%-%RANDOM%.log"
if exist "%CRYSIS_LOG%" goto newlog
set "WINEDEBUG=-all,+timestamp,+pid,+tid,+seh"
echo Crysis-only exception logging enabled for this launch.
echo Reproduce the tutorial crash, then close this diagnostic window.
echo Log file: %CRYSIS_LOG%
echo Crysis3 diagnostic launcher - no compatibility changes applied> "%CRYSIS_LOG%"
echo Runtime container: WEMU dion; executable: %CRYSIS_EXE%>> "%CRYSIS_LOG%"
"%CRYSIS_EXE%" >> "%CRYSIS_LOG%" 2>&1
set "CRYSIS_EXIT=%ERRORLEVEL%"
echo Process exit code: %CRYSIS_EXIT%>> "%CRYSIS_LOG%"
echo Process exit code: %CRYSIS_EXIT%
echo Keep the log file for analysis: %CRYSIS_LOG%
pause
exit /b %CRYSIS_EXIT%
:missing
echo Expected Crysis 3 executable or directory is not accessible in this container.
echo No game or settings were changed.
pause
exit /b 2
