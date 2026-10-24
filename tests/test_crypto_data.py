"""Host-side checks for the byte handling used during pairing."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA_CPP = ROOT / "moonlight" / "src" / "crypto" / "Data.cpp"


class CryptoDataTests(unittest.TestCase):
    def test_hex_and_random_bytes(self):
        compiler = shutil.which("g++") or shutil.which("clang++")
        vita_msys2_gcc = Path("C:/msys64/ucrt64/bin/g++.exe")
        if compiler is None and vita_msys2_gcc.exists():
            compiler = str(vita_msys2_gcc)
        if compiler is None:
            self.skipTest("A host C++ compiler is needed for Data tests")

        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / "borealis.hpp").write_text("#pragma once\n", encoding="utf-8")
            (temp / "debug.hpp").write_text(
                "#pragma once\nnamespace vita_log {\n"
                "template<class... T> void error(const char*, T...) {}\n}\n",
                encoding="utf-8",
            )
            (temp / "test.cpp").write_text(
                """
#include "Data.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

int main()
{
    const char valid[] = "0123aBfF";
    Data decoded = Data((char*)valid, 8).hex_to_bytes();
    assert(decoded.size() == 4);
    assert(decoded.bytes()[0] == 0x01 && decoded.bytes()[1] == 0x23);
    assert(decoded.bytes()[2] == 0xab && decoded.bytes()[3] == 0xff);

    Data middle = decoded.subdata(1, 2);
    assert(middle.size() == 2 && middle.bytes()[0] == 0x23 && middle.bytes()[1] == 0xab);
    assert(decoded.subdata(5, 1).is_empty());
    assert(decoded.subdata(3, 2).is_empty());
    assert(decoded.subdata(1, std::numeric_limits<size_t>::max()).is_empty());
    assert(decoded.subdata(4, 0).is_empty());
    assert(Data().subdata(0, 1).is_empty());

    Data empty;
    Data emptyCopy(empty);
    assert(emptyCopy.is_empty());
    Data assigned(decoded);
    assigned = empty;
    assert(assigned.is_empty());
    assigned = decoded;
    assert(assigned.size() == decoded.size());
    assert(std::memcmp(assigned.bytes(), decoded.bytes(), decoded.size()) == 0);

    const char odd[] = "abc";
    const char bad[] = "aX";
    const char spaced[] = "ab cd";
    assert(Data((char*)odd, 3).hex_to_bytes().is_empty());
    assert(Data((char*)bad, 2).hex_to_bytes().is_empty());
    assert(Data((char*)spaced, 5).hex_to_bytes().is_empty());
    assert(Data().hex_to_bytes().is_empty());

    Data first = Data::random_bytes(80);
    Data second = Data::random_bytes(80);
    assert(first.size() == 80 && second.size() == 80);
    assert(std::memcmp(first.bytes(), second.bytes(), 80) != 0);
    assert(Data::random_bytes(0).is_empty());

    unsigned char binaryBytes[] = {0, 10, 255, 13};
    Data sample(binaryBytes, sizeof(binaryBytes));
    sample.write_to_file("roundtrip.bin");
    Data restored = Data::read_from_file("roundtrip.bin");
    assert(restored.size() == sizeof(binaryBytes));
    assert(std::memcmp(restored.bytes(), binaryBytes, sizeof(binaryBytes)) == 0);
    assert(std::remove("roundtrip.bin") == 0);
    assert(Data::read_from_file("roundtrip.bin").is_empty());

    FILE* emptyFile = std::fopen("empty.bin", "wb");
    assert(emptyFile != nullptr);
    std::fclose(emptyFile);
    assert(Data::read_from_file("empty.bin").is_empty());
    assert(std::remove("empty.bin") == 0);
}
""",
                encoding="utf-8",
            )
            binary = temp / ("crypto_data_test.exe" if os.name == "nt" else "crypto_data_test")
            compile_cmd = [
                str(compiler),
                "-std=c++17",
                "-I", str(temp),
                "-I", str(DATA_CPP.parent),
                str(temp / "test.cpp"),
                str(DATA_CPP),
                "-o", str(binary),
            ]
            env = os.environ.copy()
            env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
            subprocess.run(compile_cmd, check=True, capture_output=True, text=True, env=env)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True, env=env, cwd=temp)


if __name__ == "__main__":
    unittest.main()
