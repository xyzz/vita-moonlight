#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$PROJECT_ROOT/scripts/vita-host.sh"
vita_host_init || exit 1

BUILD_DIR="$(vita_build_directory "${VITA_BACKEND:-gxm}")"
IP_FILE="$PROJECT_ROOT/ip_vita.txt"
PARSE_CORE="$PROJECT_ROOT/reference/vita-parse-core/vita-parse-core"
MOONLIGHT_BIN="$BUILD_DIR/moonlight.velf"
FTP_PATH="ux0:/data"
TEMP_DUMP="$PROJECT_ROOT/psp2core_tmp.psp2dmp"

if [ ! -f "$IP_FILE" ]; then
    echo "ip_vita.txt not found. Copy ip_vita.txt.example and set the Vita address." >&2
    exit 1
fi
if [ ! -x "$PARSE_CORE" ]; then
    echo "vita-parse-core not found: $PARSE_CORE" >&2
    exit 1
fi
if [ ! -f "$MOONLIGHT_BIN" ]; then
    echo "Vita symbols not found: $MOONLIGHT_BIN" >&2
    exit 1
fi

vita_ip="$(head -n 1 "$IP_FILE" | tr -d '\r\n')"
if [ -z "$vita_ip" ]; then
    echo "ip_vita.txt is empty." >&2
    exit 1
fi

latest_dump="$({
    curl --fail --silent --show-error \
        --user anonymous:anonymous \
        "ftp://${vita_ip}:1337/${FTP_PATH}/"
} | awk '/psp2core-.*\.psp2dmp/ { print $NF }' | sort | tail -n 1)"

if [ -z "$latest_dump" ]; then
    echo "No psp2core dump found on the Vita." >&2
    exit 1
fi

echo "Downloading $latest_dump..."
curl --fail --silent --show-error \
    --user anonymous:anonymous \
    --ftp-method nocwd \
    -o "$TEMP_DUMP" \
    "ftp://${vita_ip}:1337/${FTP_PATH}/${latest_dump}"

dump_path="$TEMP_DUMP"
if [ "${KEEP_PSP2DMP:-0}" = "1" ]; then
    dump_path="$PROJECT_ROOT/$latest_dump"
    mv -f "$TEMP_DUMP" "$dump_path"
fi

cleanup() {
    if [ "${KEEP_PSP2DMP:-0}" != "1" ]; then
        rm -f "$TEMP_DUMP"
    fi
}
trap cleanup EXIT

"$PARSE_CORE" "$dump_path" "$MOONLIGHT_BIN"

if [ "${KEEP_PSP2DMP:-0}" = "1" ]; then
    echo "Dump saved to $dump_path"
fi
