import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "prepare"))

import select_vitasdk_snapshot as snapshots


REQUIRED = {
    "zlib", "bzip2", "expat", "curl", "openssl", "opus", "libvita2d",
    "freetype", "libpng", "libjpeg-turbo", "zstd", "mbedtls", "sdl2",
}

POWERSHELL_AVAILABLE = bool(shutil.which("powershell.exe"))
CYGPATH_AVAILABLE = bool(shutil.which("cygpath"))


def native_windows_path(path):
    value = str(path)
    if os.name == "nt":
        return value
    return subprocess.check_output(["cygpath", "-w", value], text=True).strip()


def package_database(names):
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode="w:xz") as archive:
        for name in names:
            content = f"%NAME%\n{name}\n%VERSION%\n1.0-1\n".encode()
            member = tarfile.TarInfo(f"{name}-1/desc")
            member.size = len(content)
            archive.addfile(member, io.BytesIO(content))
    return output.getvalue()


def channel_manifest():
    return {
        "schema_version": 1,
        "channel": "2026.08",
        "world": "vita",
        "core": {"release": "sdk-snapshot-20260825.611.1"},
        "packages": {
            "repository": "vitasdk/vitasdk-autobuild",
            "release": "packages-2026.08-snapshot-20261115.68.1",
            "database": {"name": "vita.db", "sha256": "a" * 64},
        },
    }


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.installed = channel_manifest()
        self.candidate = channel_manifest()

    def test_accepts_snapshot_for_installed_channel_core_and_world(self):
        self.assertTrue(snapshots.matches_installed_sdk(self.candidate, self.installed))

    def test_rejects_other_channel_core_or_world(self):
        self.candidate["channel"] = "nightly"
        self.assertFalse(snapshots.matches_installed_sdk(self.candidate, self.installed))

        self.candidate = channel_manifest()
        self.candidate["core"]["release"] = "different-core"
        self.assertFalse(snapshots.matches_installed_sdk(self.candidate, self.installed))

        self.candidate = channel_manifest()
        self.candidate["world"] = "vita_softfp"
        self.assertFalse(snapshots.matches_installed_sdk(self.candidate, self.installed))

    def test_rejects_nightly_package_release_for_stable_channel(self):
        self.candidate["packages"]["release"] = "packages-snapshot-20261115.69.1"
        self.assertFalse(snapshots.matches_installed_sdk(self.candidate, self.installed))

    def test_reads_package_names_from_pacman_database(self):
        names = snapshots.package_names(package_database(REQUIRED))
        self.assertEqual(names, REQUIRED)
        self.assertFalse(REQUIRED - names)

    def test_finds_windows_channel_verifier_from_msys_python(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory)
            verifier = sdk / "share/vdpm/msys/usr/bin/vdpm-channel.exe"
            verifier.parent.mkdir(parents=True)
            verifier.touch()

            self.assertEqual(snapshots.find_channel_verifier(sdk), verifier)

    def test_finds_linux_channel_verifier(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory)
            verifier = sdk / "bin/vdpm-channel"
            verifier.parent.mkdir(parents=True)
            verifier.touch()

            self.assertEqual(snapshots.find_channel_verifier(sdk), verifier)

    def test_converts_msys_paths_for_native_verifier_process(self):
        with patch.object(snapshots.sys, "platform", "cygwin"):
            with patch.object(
                snapshots.subprocess,
                "check_output",
                return_value="D:\\sdk\\share\\vdpm\\channel-public-key.pem",
            ) as check_output:
                result = snapshots.native_process_argument("/d/sdk/share/vdpm/channel-public-key.pem")

        self.assertEqual(result, "D:\\sdk\\share\\vdpm\\channel-public-key.pem")
        check_output.assert_called_once_with(
            ["cygpath", "-w", "/d/sdk/share/vdpm/channel-public-key.pem"],
            text=True,
        )

    def test_incomplete_database_is_detected_by_required_package_names(self):
        names = snapshots.package_names(package_database({"zlib", "curl"}))
        self.assertEqual(REQUIRED - names, REQUIRED - {"zlib", "curl"})


@unittest.skipUnless(
    POWERSHELL_AVAILABLE and (os.name == "nt" or CYGPATH_AVAILABLE),
    "Windows PowerShell is unavailable",
)
class WindowsChannelSelectionTests(unittest.TestCase):
    def run_selection(self, staged, selected, expected):
        script = ROOT / "scripts" / "msys2" / "select-vdpm-channel.ps1"
        return subprocess.run(
            [
                "powershell.exe",
                "-NoLogo",
                "-NoProfile",
                "-NonInteractive",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                native_windows_path(script),
                native_windows_path(staged),
                native_windows_path(selected),
                native_windows_path(expected),
            ],
            capture_output=True,
            text=True,
            check=False,
        )

    def test_replaces_an_existing_channel_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            staged = root / "channel.json.staged"
            selected = root / "channel.json"
            expected = root / "candidate.json"
            staged.write_bytes(b'{"sequence": 84}\n')
            selected.write_bytes(b'{"sequence": 87}\n')
            expected.write_bytes(staged.read_bytes())

            result = self.run_selection(staged, selected, expected)

            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(selected.read_bytes(), expected.read_bytes())
            self.assertFalse(staged.exists())
            self.assertEqual(
                {path.name for path in root.iterdir()},
                {"candidate.json", "channel.json"},
            )

    def test_rejects_a_staged_manifest_for_another_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            staged = root / "channel.json.staged"
            selected = root / "channel.json"
            expected = root / "candidate.json"
            staged.write_bytes(b'{"sequence": 84}\n')
            selected.write_bytes(b'{"sequence": 87}\n')
            expected.write_bytes(b'{"sequence": 81}\n')

            result = self.run_selection(staged, selected, expected)

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(selected.read_bytes(), b'{"sequence": 87}\n')
            self.assertTrue(staged.exists())


if __name__ == "__main__":
    unittest.main()
