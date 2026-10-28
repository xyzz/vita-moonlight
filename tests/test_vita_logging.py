"""Checks for diagnostic format strings incompatible with Vita's printf logger."""

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LOGGER_CALL = re.compile(
    r'\bvita_log::(?:info|warning|error|debug|verbose)\s*\(\s*"((?:\\.|[^"\\])*)"',
    re.MULTILINE,
)
FMT_PLACEHOLDER = re.compile(r"\{(?:\}|:[^}]+\})")
NON_ENGLISH_DIAGNOSTIC = re.compile(
    r"\b(?:inicializado|inicializada|demasiado|comprobando|emparejamiento|"
    r"conexión|desconectando|terminando|aplicación|reanudando|"
    r"guardando|buscando|enviando|falló|fallo|cargando)\b",
    re.IGNORECASE,
)
VIDEO_DEBUG_LOG = re.compile(r'\bVITA_DEBUG_LOG\s*\(\s*"((?:\\.|[^"\\])*)"')


class VitaLoggingTests(unittest.TestCase):
    def test_diagnostics_use_printf_placeholders(self):
        paths = list((ROOT / "moonlight").rglob("*.cpp"))
        paths += list((ROOT / "third_party" / "gamestream").glob("*.cpp"))
        violations = []
        for path in paths:
            text = path.read_text(encoding="utf-8", errors="replace")
            for match in LOGGER_CALL.finditer(text):
                if FMT_PLACEHOLDER.search(match.group(1)):
                    line = text.count("\n", 0, match.start()) + 1
                    violations.append(f"{path.relative_to(ROOT)}:{line}: {match.group(1)}")
        self.assertFalse(violations, "\n".join(violations))

    def test_current_diagnostics_are_in_english(self):
        paths = list((ROOT / "moonlight" / "src").rglob("*.cpp"))
        paths += list((ROOT / "third_party" / "gamestream").glob("*.cpp"))
        violations = []
        for path in paths:
            if "legacy" in path.relative_to(ROOT).parts:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for pattern in (LOGGER_CALL, VIDEO_DEBUG_LOG):
                for match in pattern.finditer(text):
                    if NON_ENGLISH_DIAGNOSTIC.search(match.group(1)):
                        line = text.count("\n", 0, match.start()) + 1
                        violations.append(f"{path.relative_to(ROOT)}:{line}")
        self.assertFalse(violations, "\n".join(violations))

    def test_http_diagnostics_do_not_dump_pairing_data(self):
        source = (ROOT / "third_party" / "gamestream" / "http.cpp").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("Curl: Request:\\n{}", source)
        self.assertNotIn("Curl: Response:\\n{}", source)
        self.assertNotIn("CURLOPT_VERBOSE, 1L", source)
        self.assertNotIn("CURLOPT_DEBUGFUNCTION", source)

    def test_logger_requires_printf_format_validation(self):
        header = (ROOT / "moonlight" / "include" / "debug.hpp").read_text(
            encoding="utf-8"
        )
        for name in ("info", "warning", "error", "debug", "verbose"):
            signature = (
                f"__attribute__((format(printf, 1, 2))) inline void {name}"
                "(const char* fmt, ...)"
            )
            with self.subTest(level=name):
                self.assertIn(signature, header)


if __name__ == "__main__":
    unittest.main()
