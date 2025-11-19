#!/bin/bash

# Script para descargar el último psp2core y hacer dump con vita-parse-core
# Autor: AorsiniYT
# Uso: ./dump_psp2core.sh

set -e

# Configuración
# Leer IP desde ip_vita.txt si existe, si no usar valor por defecto
IP_FILE="$(dirname "$0")/ip_vita.txt"
if [ -f "$IP_FILE" ]; then
    FTP_HOST=$(head -n 1 "$IP_FILE" | tr -d '\r\n')
else
    FTP_HOST="192.168.0.192"
fi
FTP_PORT="1337"
FTP_USER="anonymous"
FTP_PASS="anonymous"
FTP_PATH="ux0:/data"
LOCAL_TMP="psp2core_tmp.psp2dmp"
PARSE_CORE="../vita-parse-core/vita-parse-core"
MOONLIGHT_BIN="./build/moonlight.elf"

# 1. Listar archivos psp2core-*.psp2dmp en la Vita
FILE_LIST=$(curl -s --user "$FTP_USER:$FTP_PASS" "ftp://$FTP_HOST:$FTP_PORT/$FTP_PATH/" | grep 'psp2core-.*.psp2dmp' | awk '{print $NF}')

if [ -z "$FILE_LIST" ]; then
    echo "No se encontraron archivos psp2core en la Vita."
    exit 1
fi

# 2. Encontrar el más reciente (por nombre, asumiendo que el nombre lleva timestamp)
LATEST_FILE=$(echo "$FILE_LIST" | sort | tail -n 1)
echo "Archivo más reciente: $LATEST_FILE"

# 3. Descargar el archivo
curl -s --user "$FTP_USER:$FTP_PASS" -Q "CWD $FTP_PATH" "ftp://$FTP_HOST:$FTP_PORT/$LATEST_FILE" -o "$LOCAL_TMP"

if [ ! -f "$LOCAL_TMP" ]; then
    echo "Error al descargar el archivo."
    exit 2
fi

echo "Ejecutando vita-parse-core..."
$PARSE_CORE "$LOCAL_TMP" "$MOONLIGHT_BIN"

rm "$LOCAL_TMP"
echo "Dump completado."
