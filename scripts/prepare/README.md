# Vita dependency setup

`install_all.sh` installs the libraries required by Moonlight into the active
VitaSDK sysroot. It supports native Linux, WSL and Windows/MSYS2.

The script installs standard packages through VDPM and builds the project-specific
FFmpeg-vita package. This FFmpeg fork provides the `h264_vita` decoder and Vita
pixel formats used by the low-latency video path. The package recipe pins the
tested FFmpeg-vita revision before building it.

## Requirements

Set `VITASDK` before running the script:

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
```

The SDK must provide `vita-makepkg` and `vdpm`. The host also needs Bash, CMake,
Make, Git, curl, unzip, bsdtar, pkg-config, fakeroot, patch and the standard Unix
text utilities.

### Linux and WSL

On Debian or Ubuntu:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git curl wget unzip \
    libarchive-tools pkg-config fakeroot patch python3 bzip2 xz-utils
```

For WSL, install VitaSDK inside the Linux distribution. A VitaSDK copied from
Windows is not supported because its host tools target a different environment.
When the repository is under `/mnt`, set `VITA_BUILD_ROOT` to a directory in the
Linux filesystem to avoid slow incremental builds:

```bash
export VITA_BUILD_ROOT="$HOME/.cache/vita-moonlight-build"
```

### Windows/MSYS2

Use the MSYS2 `MSYS` shell:

```bash
pacman -S --needed make git curl wget unzip libarchive cmake ninja pkgconf \
    patch python diffutils sed grep findutils tar
```

The repository includes the MSYS2 fakeroot compatibility wrapper used by
`vita-makepkg`. The scripts also repair missing SDK compatibility symlinks when
Windows cannot create them.

## Install dependencies

```bash
./scripts/prepare/install_all.sh
```

Installed VDPM packages include zlib, bzip2, expat, curl, OpenSSL, Opus,
libvita2d, FreeType, libpng, libjpeg-turbo, zstd, mbedTLS and SDL2. After those
packages are ready, the script builds and installs FFmpeg-vita.

Build directories and package archives are removed after installation. Preserve
them for inspection with:

```bash
./scripts/prepare/install_all.sh --keep-build
```

## Optional GLES support

The GXM backend does not require PVR_PSP2. To build the GLES backend, install its
headers, libraries and runtime modules with:

```bash
./scripts/prepare/install_all.sh --with-pvr
./makepsv --gl --release --no-deploy
```

`install_psv2` is called automatically by `--with-pvr`; it is not part of the
normal GXM dependency installation.

## Verification

The installer verifies that the installed FFmpeg headers contain
`AV_PIX_FMT_VITA` and that `libavcodec.a` contains `h264_vita`. A normal release
build can then be tested with:

```bash
./makepsv --gxm --release --no-deploy
```
