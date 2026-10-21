"""Behavioral tests for makepsv; all builds and Vita I/O are mocked."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def locate_bash():
    if os.name == "nt":
        for path in (
            Path(r"C:\msys64\usr\bin\bash.exe"),
            Path(r"C:\msys32\usr\bin\bash.exe"),
            Path(r"C:\Program Files\Git\bin\bash.exe"),
        ):
            if path.is_file():
                return str(path)
        return None
    return shutil.which("bash")


FAKE_VITA_HOST = r"""#!/usr/bin/env bash
vita_host_init() { VITA_HOST=mock; VITA_BUILD_JOBS=2; return 0; }
vita_print_environment() { :; }
vita_require_tools() { return 0; }
vita_require_sdk() { return 0; }
vita_fix_msys2_sdk_aliases() { return 0; }
vita_set_build_flags() { :; }
vita_reset_stale_cmake_cache() { return 0; }
vita_build_directory() { printf '%s\n' "vpk-out"; }
vita_configure_project() { printf 'configure:%s\n' "$*" >> trace.log; }
vita_title_id() { printf 'XYZZ00002\n'; }
vita_ping() { [ "${MOCK_PING_FAILURE:-0}" != "1" ]; }
vita_open_dir() { printf 'open:%s\n' "$*" >> trace.log; }
cmake() {
    printf 'cmake:%s\n' "$*" >> trace.log
    if [ "$1" = "--build" ]; then
        mkdir -p "$2"
        printf 'mock-vpk' > "$2/moonlight.vpk"
    fi
}
curl() {
    printf 'curl:%s\n' "$*" >> trace.log
    case " $* " in
        *" -T "*) [ "${MOCK_UPLOAD_FAILURE:-0}" != "1" ] ;;
        *) return 0 ;;
    esac
}
nc() {
    printf 'nc:%s\n' "$*" >> trace.log
    return 0
}
"""


@unittest.skipUnless(locate_bash(), "Bash is required for shell integration tests")
class MakepsvTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.work = Path(self.tmp.name)
        (self.work / "scripts").mkdir()
        shutil.copyfile(ROOT / "makepsv", self.work / "makepsv")
        (self.work / "scripts" / "vita-host.sh").write_text(
            FAKE_VITA_HOST, encoding="utf-8", newline="\n"
        )

    def run_builder(self, *args, selection="", **environment):
        env = os.environ.copy()
        if os.name == "nt":
            # Native Python inherits a Windows PATH, which does not contain
            # MSYS2/Git Bash coreutils such as dirname, date, head and tr.
            env["PATH"] = "/usr/bin:/bin:" + env.get("PATH", "")
        env.update(environment)
        result = subprocess.run(
            [locate_bash(), "./makepsv", *args],
            cwd=self.work, env=env, input=selection.encode("utf-8"),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20,
        )
        # Binary stdin prevents native Windows Python from adding CR to the
        # selections; MSYS2 Bash would interpret `1\\r` as an invalid menu item.
        result.stdout = result.stdout.decode("utf-8", errors="replace")
        result.stderr = result.stderr.decode("utf-8", errors="replace")
        return result

    def trace(self):
        path = self.work / "trace.log"
        return path.read_text(encoding="utf-8") if path.is_file() else ""

    def add_vita_ip(self):
        (self.work / "ip_vita.txt").write_text("192.0.2.18\n", encoding="utf-8")

    def test_build_only_never_needs_ip_or_ftp(self):
        result = self.run_builder("--gxm", "--release", "--no-deploy")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.work / "vpk-out" / "moonlight.vpk").is_file())
        self.assertIn("cmake:--build", self.trace())
        self.assertNotIn("curl:", self.trace())
        self.assertNotIn("nc:", self.trace())

    def test_noninteractive_arguments_upload_vpk(self):
        self.add_vita_ip()
        result = self.run_builder("--gxm", "--release")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("VPK uploaded", result.stdout)
        self.assertIn("curl:--ftp-method nocwd -T", self.trace())
        self.assertIn("nc:192.0.2.18 1338", self.trace())

    def test_explicit_deploy_uploads(self):
        self.add_vita_ip()
        result = self.run_builder("--deploy")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("VPK uploaded", result.stdout)

    def test_deploy_without_ip_fails_after_successful_build(self):
        result = self.run_builder("--gxm", "--release")
        self.assertEqual(result.returncode, 6)
        self.assertIn("Build completed", result.stdout)
        self.assertIn("ip_vita.txt not found", result.stderr)
        self.assertNotIn("curl:", self.trace())

    def test_upload_failure_propagates_nonzero_exit_code(self):
        self.add_vita_ip()
        result = self.run_builder("--gxm", MOCK_UPLOAD_FAILURE="1")
        self.assertEqual(result.returncode, 6)
        self.assertNotIn("VPK uploaded", result.stdout)
        self.assertIn("curl:--ftp-method nocwd -T", self.trace())

    def test_interactive_upload_still_works(self):
        self.add_vita_ip()
        result = self.run_builder(selection="1\n1\n1\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("VPK uploaded", result.stdout)

    def test_interactive_exit_does_not_upload(self):
        result = self.run_builder(selection="1\n1\n3\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("curl:", self.trace())

    def test_help_does_not_build_or_deploy(self):
        result = self.run_builder("--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("--no-deploy", result.stdout)
        self.assertNotIn("cmake:", self.trace())


if __name__ == "__main__":
    unittest.main()
