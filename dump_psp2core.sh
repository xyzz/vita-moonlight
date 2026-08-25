#!/bin/bash

# Script para descargar el último psp2core y hacer dump con vita-parse-core
# Autor: AorsiniYT
# Uso: ./dump_psp2core.sh

set -e

ROOTDIR="$(cd "$(dirname "$0")" && pwd)"
source "${ROOTDIR}/scripts/vita-host.sh"
vita_host_init || exit 1
BUILD_DIR="$(vita_build_directory "${VITA_BACKEND:-gxm}")"

IP_FILE="${ROOTDIR}/ip_vita.txt"
if [ ! -f "$IP_FILE" ]; then
    echo "ip_vita.txt not found. Copy ip_vita.txt.example and set the Vita address." >&2
    exit 1
fi
FTP_HOST="$(head -n 1 "$IP_FILE" | tr -d '\r\n')"
if [ -z "$FTP_HOST" ]; then
    echo "ip_vita.txt is empty." >&2
    exit 1
fi
FTP_PORT="1337"
FTP_USER="anonymous"
FTP_PASS="anonymous"
FTP_PATH="ux0:/data"
LOCAL_TMP="psp2core_tmp.psp2dmp"
PARSE_CORE="${ROOTDIR}/reference/vita-parse-core/vita-parse-core"
MOONLIGHT_BIN="${BUILD_DIR}/moonlight.velf"

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

# If KEEP_PSP2DMP=1, preserve the downloaded dump under its remote filename so other tools
# that search for psp2core-*.psp2dmp will find it. We also update LOCAL_TMP to point to
# the preserved filename so vita-parse-core runs over the preserved file.
if [ "${KEEP_PSP2DMP:-0}" = "1" ]; then
    cp -v "$LOCAL_TMP" "$LATEST_FILE"
    LOCAL_TMP="$LATEST_FILE"
    echo "KEEP_PSP2DMP=1: preserved downloaded dump as $LATEST_FILE"
fi

if [ ! -f "$LOCAL_TMP" ]; then
    echo "Error al descargar el archivo."
    exit 2
fi

echo "Ejecutando vita-parse-core..."
$PARSE_CORE "$LOCAL_TMP" "$MOONLIGHT_BIN"

# By default we remove the temporary downloaded psp2 dump after parsing.
# Set KEEP_PSP2DMP=1 in the environment to preserve the downloaded file for manual inspection.
if [ "${KEEP_PSP2DMP:-0}" = "1" ]; then
    echo "KEEP_PSP2DMP=1 set; preserving downloaded psp2 dump at: $LOCAL_TMP"
    echo "Dump completado."
else
    rm "$LOCAL_TMP"
    echo "Dump completado and temporary file removed."
fi
