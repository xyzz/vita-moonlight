"""Unit tests for combined Moonlight Vita release asset validation."""
import importlib.util
import tempfile
import unittest
import zipfile
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "check_vpk.py"
spec = importlib.util.spec_from_file_location("check_vpk", SCRIPT)
check_vpk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check_vpk)


class VpkPackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "moonlight.vpk"

    def write_vpk(self, remove=(), overrides=None):
        overrides = overrides or {}
        with zipfile.ZipFile(self.path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name in check_vpk.REQUIRED_FILES:
                if name not in remove:
                    archive.writestr(name, overrides.get(name, name.encode("utf-8")))
        return self.path

    def test_complete_combined_package(self):
        self.write_vpk()
        self.assertEqual(check_vpk.validate_vpk(self.path), [])

    def test_missing_legacy_or_borealis_is_rejected(self):
        for filename in ("legacy.self", "borealis.self", "eboot.bin"):
            with self.subTest(filename=filename):
                self.write_vpk(remove=(filename,))
                self.assertIn(
                    f"Missing packaged file: {filename}",
                    check_vpk.validate_vpk(self.path),
                )

    def test_empty_executable_is_rejected(self):
        self.write_vpk(overrides={"legacy.self": b""})
        self.assertIn("Empty packaged file: legacy.self", check_vpk.validate_vpk(self.path))

    def test_duplicate_frontend_payload_is_rejected(self):
        self.write_vpk(overrides={"legacy.self": b"same", "borealis.self": b"same"})
        self.assertIn(
            "Launcher, Legacy and Borealis must be distinct executables",
            check_vpk.validate_vpk(self.path),
        )

    def test_invalid_archive_is_rejected(self):
        self.path.write_bytes(b"not-a-VPK")
        self.assertTrue(any("Unreadable VPK:" in error for error in check_vpk.validate_vpk(self.path)))

    def test_nonexistent_archive_is_rejected(self):
        self.assertTrue(any("does not exist" in error for error in check_vpk.validate_vpk(self.path)))

    def test_cli_exit_codes(self):
        self.write_vpk()
        self.assertEqual(check_vpk.main([str(self.path)]), 0)
        self.assertEqual(check_vpk.main([str(self.path) + ".missing"]), 1)


if __name__ == "__main__":
    unittest.main()
