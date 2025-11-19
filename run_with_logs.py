import subprocess
import sys
import os

BUILD_DIR = "build_mingw"
APP_NAME = "moonlight.exe"

exe_path = os.path.join(BUILD_DIR, APP_NAME)
if not os.path.exists(exe_path):
    print(f"[ERROR] No se encontró el ejecutable: {exe_path}")
    sys.exit(1)

print(f"[INFO] Ejecutando: {exe_path}")
process = subprocess.Popen(
    [exe_path],
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    cwd=BUILD_DIR,
    universal_newlines=True,
    bufsize=1,
    encoding="utf-8",      # Fuerza decodificación UTF-8
    errors="replace"       # Reemplaza caracteres inválidos
)
for line in process.stdout:
    print(line, end="")
process.wait()
