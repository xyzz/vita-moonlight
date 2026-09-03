import datetime
import os
import subprocess
import sys
from pathlib import Path


project_root = Path(sys.argv[1] if len(sys.argv) > 1 else Path.cwd()).resolve()
build_dir = project_root / "build_mingw"
executable = build_dir / "moonlight.exe"
log_dir = project_root / "logs"

if not executable.is_file():
    raise SystemExit(f"Executable not found: {executable}")

log_dir.mkdir(exist_ok=True)
timestamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
log_path = log_dir / f"moonlight-{timestamp}.log"

environment = os.environ.copy()
environment.setdefault("BOREALIS_RES_FOLDER", str(build_dir / "resources"))

with log_path.open("w", encoding="utf-8") as log_file:
    process = subprocess.Popen(
        [str(executable)],
        cwd=build_dir,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    assert process.stdout is not None
    for line in process.stdout:
        print(line, end="")
        log_file.write(line)

exit_code = process.wait()
print(f"Log saved to {log_path}")
raise SystemExit(exit_code)
