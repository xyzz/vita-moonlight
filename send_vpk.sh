#!/bin/bash

# Script para enviar el VPK de OSRS a PS Vita
# Lee la IP desde ip_vita.txt si PSVITAIP no está definido

# Parsear argumentos
VPK_FILE=""
USE_GL_BUILD=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --file)
            VPK_FILE="$2"
            shift 2
            ;;
        -gl)
            USE_GL_BUILD=true
            shift
            ;;
        *)
            echo "Uso: $0 [--file <ubicacion_del_archivo>] [-gl]"
            exit 1
            ;;
    esac
done

if [ -z "$VPK_FILE" ]; then
    if [ "$USE_GL_BUILD" = true ]; then
        VPK_FILE="cmake-build-psv-gl/moonlight.vpk"
    else
        VPK_FILE="cmake-build-psv/moonlight.vpk"
    fi
fi

if [ -z "$PSVITAIP" ]; then
    if [ -f "ip_vita.txt" ]; then
        PSVITAIP=$(cat ip_vita.txt | tr -d '\n')
    else
        echo "Error: Define PSVITAIP o crea ip_vita.txt con la IP"
        exit 1
    fi
fi

if [ ! -f "$VPK_FILE" ]; then
    echo "Error: No se encuentra $VPK_FILE."
    exit 1
fi

echo "Enviando VPK: $VPK_FILE..."
curl --ftp-create-dirs -T "$VPK_FILE" ftp://${PSVITAIP}:1337/ux0:/ABM/moonlight.vpk

    # 3. Lanzar la app
echo "[+] Enviando comando de lanzamiento..."
LAUNCH_RESPONSE=$(echo launch VITASHELL | nc "$PSVITAIP" 1338)
echo "[+] Respuesta de vitacompanion: $LAUNCH_RESPONSE"
if echo "$LAUNCH_RESPONSE" | grep -iq launched; then
    echo "[+] ¡Aplicación lanzada exitosamente!"
else
    echo "[+] Advertencia: No se recibió confirmación de lanzamiento."
fi
