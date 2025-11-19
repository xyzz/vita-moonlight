@echo off
setlocal enabledelayedexpansion

echo [INFO] Iniciando Moonlight Vita con captura de logs...

:: Configuración
set "APP_NAME=moonlight.exe"
set "BUILD_DIR=build_mingw"
set "LOG_FILE=logs_%date:/=%%time::=-%.txt"
set "LOG_FILE=%LOG_FILE: =0%"
set "LOG_FILE=%LOG_FILE:,=.%"

:: Crear directorio de logs si no existe
if not exist "logs" mkdir logs

:: Configurar variables de entorno para depuración
set "BOREALIS_LOG_LEVEL=0"
set "BOREALIS_LOG_MASK=*"
set "BOREALIS_LOG_STDOUT=1"
set "BOREALIS_LOG_FILE=1"
set "BOREALIS_LOG_VERBOSE=1"
set "BOREALIS_RES_FOLDER=%CD%\%BUILD_DIR%\resources"  ; Ruta a recursos en build_mingw

:: Mostrar información de depuración
echo [DEBUG] Directorio actual: %CD%
echo [DEBUG] Variables de entorno configuradas:
echo   BOREALIS_LOG_LEVEL=%BOREALIS_LOG_LEVEL%
echo   BOREALIS_LOG_MASK=%BOREALIS_LOG_MASK%
echo   BOREALIS_LOG_STDOUT=%BOREALIS_LOG_STDOUT%
echo   BOREALIS_LOG_VERBOSE=%BOREALIS_LOG_VERBOSE%
echo   BOREALIS_LANG=%BOREALIS_LANG%
echo   BOREALIS_RES_FOLDER=%BOREALIS_RES_FOLDER%
echo ================================================

:: Verificar si el ejecutable existe
if not exist "%BUILD_DIR%\%APP_NAME%" (
    echo [ERROR] No se encontró el ejecutable: %CD%\%BUILD_DIR%\%APP_NAME%
    echo [INFO] Por favor, asegúrate de que el archivo existe y de que BUILD_DIR esté configurado correctamente.
    pause
    exit /b 1
)

:: Verificar recursos
if not exist "%BOREALIS_RES_FOLDER%" (
    echo [ERROR] No se encontró la carpeta de recursos: %BOREALIS_RES_FOLDER%
    pause
    exit /b 1
)

:: Iniciar la aplicación mostrando logs en tiempo real y guardando en archivo
pushd %BUILD_DIR%

:: Ejecutar y mostrar logs en tiempo real (Windows CMD o Git Bash)
if exist "%SystemRoot%\System32\tee.exe" (
    "%APP_NAME%" 2>&1 | tee "..\logs\%LOG_FILE%"
) else (
    popd
    python run_with_logs.py
    pushd %BUILD_DIR%
)
set "EXIT_CODE=%ERRORLEVEL%"
popd

echo.
echo [INFO] Los logs se guardaron en: logs\%LOG_FILE%
echo Presiona ENTER para salir...
pause >nul
exit /b %EXIT_CODE%
