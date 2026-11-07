import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCES = (
    ROOT / "third_party" / "mdnsniff" / "tests" / "test_mdns_parser.c",
    ROOT / "legacy" / "third_party" / "mdnsniff" / "tests" / "test_mdns_parser.c",
)


class MdnsParserTests(unittest.TestCase):
    def test_malformed_packets_and_valid_names(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if not compiler and os.name == "nt":
            candidate = Path("C:/msys64/ucrt64/bin/gcc.exe")
            if candidate.is_file():
                compiler = str(candidate)
        if not compiler:
            self.skipTest("C compiler not installed")

        env = os.environ.copy()
        compiler_dir = str(Path(compiler).resolve().parent)
        env["PATH"] = compiler_dir + os.pathsep + env.get("PATH", "")

        with tempfile.TemporaryDirectory(prefix="moonlight-mdns-") as folder:
            for number, source in enumerate(SOURCES):
                with self.subTest(source=str(source.relative_to(ROOT))):
                    output = Path(folder) / f"mdns-parser-{number}"
                    if os.name == "nt":
                        output = output.with_suffix(".exe")
                    compile_result = subprocess.run(
                        [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                         "-pedantic", str(source), "-o", str(output)],
                        capture_output=True, text=True, env=env, timeout=45,
                    )
                    self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
                    result = subprocess.run(
                        [str(output)], capture_output=True, text=True, env=env, timeout=10,
                    )
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("mDNS parser regression tests passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
