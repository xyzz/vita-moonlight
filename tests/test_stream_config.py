"""Host-side regression tests for stream configuration defaults and limits."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "moonlight" / "src" / "session" / "stream_config.cpp"


class StreamConfigTests(unittest.TestCase):
    def test_bitrate_and_configuration(self):
        compiler = shutil.which("g++") or shutil.which("clang++")
        msys2_gcc = Path("C:/msys64/ucrt64/bin/g++.exe")
        if compiler is None and msys2_gcc.is_file():
            compiler = str(msys2_gcc)
        if compiler is None:
            self.skipTest("Host C++ compiler unavailable")

        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            (tmp / "ConfigManager.hpp").write_text(
                "#pragma once\n"
                "struct StreamConfiguration {\n"
                "    int streamWidth=960, streamHeight=544, fps=60;\n"
                "    int bitrate=-1, packetSize=1024, streamingRemotely=0;\n"
                "};\n",
                encoding="utf-8",
            )
            (tmp / "Limelight.h").write_text(
                "#pragma once\n"
                "#include <cstring>\n"
                "#define STREAM_CFG_LOCAL 0\n"
                "#define STREAM_CFG_REMOTE 1\n"
                "#define STREAM_CFG_AUTO 2\n"
                "#define AUDIO_CONFIGURATION_STEREO 2\n"
                "#define VIDEO_FORMAT_H264 1\n"
                "#define ENCFLG_NONE 0\n"
                "struct STREAM_CONFIGURATION {\n"
                "  int width,height,fps,bitrate,packetSize,streamingRemotely;\n"
                "  int audioConfiguration,supportedVideoFormats,clientRefreshRateX100,encryptionFlags;\n"
                "};\n"
                "inline void LiInitializeStreamConfiguration(STREAM_CONFIGURATION* p) {\n"
                "  std::memset(p, 0, sizeof(*p));\n"
                "}\n",
                encoding="utf-8",
            )
            (tmp / "test.cpp").write_text(
                r"""
#include "session/stream_config.hpp"
#include <algorithm>
#include <cassert>
#include <climits>

int main()
{
    using namespace stream;
    assert(supportedStreamResolutions().size() == 8);
    assert(supportedHostResolutions().size() == 9);
    assert(isSupportedStreamResolution(960, 544));
    assert(!isSupportedStreamResolution(1920, 1080));
    assert(streamResolutionIndex(960, 544) == 2);
    assert(streamResolutionIndex(999, 999, 999) == 7);
    assert(hostResolutionIndex(1920, 1080) == 8);
    assert(frameRateIndex(60) == 4);
    assert(frameRateIndex(999, 99) == 4);
    assert(bitrateIndex(-1) == 0);

    assert(calculateAutoBitrate(960, 544, 60) == 5640);
    assert(calculateAutoBitrate(1280, 720, 60) == 9953);
    assert(calculateAutoBitrate(1, 1, 1) == 5000);
    assert(calculateAutoBitrate(0, 720, 60) == 5000);
    assert(calculateAutoBitrate(-1, 720, 60) == 5000);
    assert(calculateAutoBitrate(INT_MAX, INT_MAX, INT_MAX) == 20000);
    assert(calculateAutoBitrate(1920, 1080, 1000) == 20000);
    for (const auto& resolution : supportedStreamResolutions()) {
        for (int fps : supportedFrameRates()) {
            const long long pixels = static_cast<long long>(resolution.width) * resolution.height * fps;
            const int expected = static_cast<int>(pixels * 18 / 100000);
            assert(calculateAutoBitrate(resolution.width, resolution.height, fps) ==
                std::max(5000, std::min(20000, expected)));
        }
    }

    StreamConfiguration defaults;
    STREAM_CONFIGURATION config = buildConfiguration(defaults);
    assert(config.width == 960 && config.height == 544);
    assert(config.fps == 60 && config.bitrate == 5640);
    assert(config.streamingRemotely == STREAM_CFG_LOCAL);
    assert(config.packetSize == 1024);
    assert(config.audioConfiguration == AUDIO_CONFIGURATION_STEREO);
    assert(config.supportedVideoFormats == VIDEO_FORMAT_H264);

    defaults.fps = 0;
    defaults.bitrate = 0;
    defaults.packetSize = 0;
    defaults.streamingRemotely = 99;
    config = buildConfiguration(defaults);
    assert(config.fps == 60 && config.bitrate == 6000);
    assert(config.packetSize == 1024);
    assert(config.streamingRemotely == STREAM_CFG_LOCAL);

    defaults.streamingRemotely = STREAM_CFG_REMOTE;
    config = buildConfiguration(defaults);
    assert(config.streamingRemotely == STREAM_CFG_REMOTE);
    defaults.streamingRemotely = STREAM_CFG_AUTO;
    config = buildConfiguration(defaults);
    assert(config.streamingRemotely == STREAM_CFG_AUTO);
}
""",
                encoding="utf-8",
            )
            executable = tmp / ("stream_config_test.exe" if os.name == "nt" else "stream_config_test")
            env = os.environ.copy()
            env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
            compiled = subprocess.run(
                [
                    str(compiler),
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I", str(tmp),
                    "-I", str(ROOT / "moonlight" / "include"),
                    str(tmp / "test.cpp"),
                    str(SOURCE),
                    "-o", str(executable),
                ],
                check=False, capture_output=True, text=True, env=env,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            subprocess.run(
                [str(executable)], check=True, capture_output=True,
                text=True, env=env, cwd=tmp,
            )


if __name__ == "__main__":
    unittest.main()
