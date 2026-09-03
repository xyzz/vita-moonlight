@echo off
setlocal

for %%I in ("%~dp0\..\..\..") do set "PROJECT_ROOT=%%~fI"
set "BUILD_DIR=%PROJECT_ROOT%\build_mingw"
set "APP_NAME=moonlight.exe"

if not exist "%BUILD_DIR%\%APP_NAME%" (
    echo Executable not found: %BUILD_DIR%\%APP_NAME%
    exit /b 1
)

if not exist "%PROJECT_ROOT%\logs" mkdir "%PROJECT_ROOT%\logs"

set "BOREALIS_LOG_LEVEL=0"
set "BOREALIS_LOG_MASK=*"
set "BOREALIS_LOG_STDOUT=1"
set "BOREALIS_LOG_VERBOSE=1"
set "BOREALIS_RES_FOLDER=%BUILD_DIR%\resources"

python "%~dp0run_with_logs.py" "%PROJECT_ROOT%"
