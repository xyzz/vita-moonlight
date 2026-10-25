"""Host-side AES regression tests for the pairing encryption format."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CRYPTO = ROOT / "moonlight" / "src" / "crypto"


class PairingAesTests(unittest.TestCase):
    def test_aes_ecb_blocks_and_invalid_input(self):
        compiler = shutil.which("g++")
        if os.name == "nt":
            msys2_gcc = Path("C:/msys64/ucrt64/bin/g++.exe")
            if not msys2_gcc.is_file():
                self.skipTest("MSYS2 UCRT64 OpenSSL toolchain unavailable")
            compiler = str(msys2_gcc)
        elif compiler is None:
            self.skipTest("Host C++ compiler unavailable")

        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            (tmp / "borealis.hpp").write_text("#pragma once\n", encoding="utf-8")
            (tmp / "debug.hpp").write_text(
                "#pragma once\nnamespace vita_log {\n"
                "template<class... T> void error(const char*, T...) {}\n"
                "}\n",
                encoding="utf-8",
            )
            (tmp / "test.cpp").write_text(
                r"""
#include "OpenSSLCryptoManager.hpp"
#include <cassert>
#include <cstring>

int main()
{
    // AES-128 known-answer test from FIPS 197.
    unsigned char keyBytes[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
    };
    unsigned char plainBytes[] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
    };
    unsigned char expected[] = {
        0x69, 0xC4, 0xE0, 0xD8, 0x6A, 0x7B, 0x04, 0x30,
        0xD8, 0xCD, 0xB7, 0x80, 0x70, 0xB4, 0xC5, 0x5A
    };

    Data key(keyBytes, sizeof(keyBytes));
    Data plain(plainBytes, sizeof(plainBytes));
    Data encrypted = OpenSSLCryptoManager::aes_encrypt(plain, key);
    assert(encrypted.size() == sizeof(expected));
    assert(std::memcmp(encrypted.bytes(), expected, sizeof(expected)) == 0);
    Data decrypted = OpenSSLCryptoManager::aes_decrypt(encrypted, key);
    assert(decrypted.size() == plain.size());
    assert(std::memcmp(decrypted.bytes(), plainBytes, sizeof(plainBytes)) == 0);

    unsigned char shortPlain[17] = {};
    shortPlain[0] = 0x42;
    shortPlain[16] = 0xF0;
    Data padded = OpenSSLCryptoManager::aes_encrypt(
        Data(shortPlain, sizeof(shortPlain)), key);
    assert(padded.size() == 32);
    Data unpadded = OpenSSLCryptoManager::aes_decrypt(padded, key);
    assert(unpadded.size() == 32);
    assert(std::memcmp(unpadded.bytes(), shortPlain, sizeof(shortPlain)) == 0);
    for (size_t i = sizeof(shortPlain); i < unpadded.size(); ++i)
        assert(unpadded.bytes()[i] == 0);

    Data shortKey(keyBytes, sizeof(keyBytes) - 1);
    assert(OpenSSLCryptoManager::aes_encrypt(plain, shortKey).is_empty());
    assert(OpenSSLCryptoManager::aes_decrypt(encrypted, shortKey).is_empty());
    assert(OpenSSLCryptoManager::aes_encrypt(Data(), key).is_empty());
    assert(OpenSSLCryptoManager::aes_decrypt(Data(), key).is_empty());
    assert(OpenSSLCryptoManager::aes_decrypt(
        Data(expected, sizeof(expected) - 1), key).is_empty());
}
""",
                encoding="utf-8",
            )
            output = tmp / ("test_aes.exe" if os.name == "nt" else "test_aes")
            cmd = [
                compiler,
                "-std=c++17", "-DUSE_OPENSSL_CRYPTO",
                "-I", str(tmp), "-I", str(CRYPTO),
                str(tmp / "test.cpp"),
                str(CRYPTO / "OpenSSLCryptoManager.cpp"),
                str(CRYPTO / "Data.cpp"),
                "-o", str(output), "-lcrypto",
            ]
            env = os.environ.copy()
            env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
            compiled = subprocess.run(cmd, capture_output=True, text=True, env=env)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            subprocess.run(
                [str(output)], check=True, capture_output=True, text=True,
                cwd=tmp, env=env,
            )


if __name__ == "__main__":
    unittest.main()
