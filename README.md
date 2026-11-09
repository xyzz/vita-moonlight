![Moonlight Vita Logo](resources/img/icon.jpg)

# Moonlight Vita

Moonlight Vita is a Moonlight client for PS Vita and PSTV, based on Moonlight
Embedded. It streams games and applications from a Sunshine-compatible host.

## Features

- Hardware H.264 decoding on PS Vita
- Low-latency GXM presentation
- Host discovery, pairing and Wake-on-LAN
- Touchscreen, rear touchpad, motion and configurable shortcuts
- Optional microphone forwarding through Moonmic
- Selectable Legacy and Borealis frontends

## Requirements

- PS Vita or PSTV with homebrew support
- VitaShell for VPK installation
- A PC running Sunshine or another compatible GameStream host
- A stable local network shared by the console and host PC

## Network and bitrate

The handheld **PS Vita connects over Wi-Fi** and has no built-in Ethernet port.
**PS TV** supports Wi-Fi and wired 10/100 Mbps Ethernet.

Keep the console and host PC on the same local network. Connecting the host PC
to the router with Ethernet is recommended. PS TV can also use a wired
connection; a handheld PS Vita uses Wi-Fi.

For the handheld PS Vita, **5,000 Kbps is a sensible conservative starting
point**. If the stream remains stable, try 8,000–10,000 Kbps for better image
quality. You can also leave bitrate on Automatic; at the default 720p60 setting,
the current automatic calculation is approximately 10 Mbps. The best value
depends on Wi-Fi conditions, resolution and frame rate.

## Installation

1. Download the VPK from the [Moonlight Vita releases](https://github.com/xyzz/vita-moonlight/releases) and copy it to the Vita.
2. Install the package with VitaShell.
3. Start Moonlight from LiveArea and pair it with the host.

## Controller shortcuts

The default controller combinations are the same in both frontends:

| Action | Legacy | Borealis default |
| --- | --- | --- |
| Open the pause menu | `START + L1 + R1` | `START + L1 + R1` |
| Open the keyboard | `START + LEFT` | `START + LEFT` |

In **Borealis**, open **Settings → Configure Shortcuts** to change or disable the pause and keyboard combinations. Borealis also supports additional custom shortcuts. **Legacy** uses the combinations shown above as fixed shortcuts and does not currently provide shortcut remapping.

The keyboard shortcut opens the keyboard mode configured for the selected frontend; it is not tied to one specific keyboard presentation.

## Building

Linux, WSL and Windows/MSYS2 use the same build scripts. Install VitaSDK first
and make sure `VITASDK/bin` is available in `PATH`.

Clone the main Moonlight Vita repository and its submodules:

```bash
git clone --recurse-submodules https://github.com/xyzz/vita-moonlight.git
cd vita-moonlight
```

After pulling changes that update submodules, run `git submodule update --init --recursive`.

### Linux

Install the host tools. On Debian or Ubuntu:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git curl wget unzip \
    libarchive-tools pkg-config fakeroot patch python3 bzip2 xz-utils
```

Install VitaSDK following the [official VitaSDK instructions](https://vitasdk.org/),
then export its location:

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
```

### WSL

Use a current WSL2 distribution and install the same packages listed for Linux.
Install VitaSDK inside WSL, normally at `/usr/local/vitasdk`; do not reuse a
Windows VitaSDK installation. Keeping the repository in the WSL filesystem
(for example `~/src/vita-moonlight`) avoids slow builds and permission issues on
`/mnt/c`.

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
```

If the source must remain on a mounted Windows drive, place build outputs in the
WSL filesystem:

```bash
export VITA_BUILD_ROOT="$HOME/.cache/vita-moonlight-build"
```

### Windows/MSYS2

Run the commands from the MSYS2 `MSYS` shell. Install the required host tools:

```bash
pacman -S --needed make git curl wget unzip libarchive cmake ninja pkgconf \
    patch python diffutils sed grep findutils tar
```

Set `VITASDK` to the installed SDK. POSIX and Windows paths are both accepted:

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
```

### Dependencies

Install the Vita libraries and the FFmpeg-vita fork:

```bash
./scripts/prepare/install_all.sh
```

The normal GXM build does not require PVR_PSP2. Install it only for the optional
GLES backend:

```bash
./scripts/prepare/install_all.sh --with-pvr
```

### Compile

To build a release VPK with GXM without deploying it:

```bash
./makepsv --gxm --release --no-deploy
```

Run `./makepsv` without arguments to open the interactive builder. Build
directory names include the host and graphics backend, such as
`cmake-build-psv-linux-gxm`, `cmake-build-psv-wsl-gxm` or
`cmake-build-psv-msys2-gxm`. Set `VITA_BUILD_ROOT` to place these directories
outside the source tree.

For incremental builds that can also deploy to the Vita, copy the IP template and enter your Vita's address:

```bash
cp ip_vita.txt.example ip_vita.txt
./makefast
```

`makefast --no-deploy` only builds the application and does not require an IP
file. Deployment expects VitaShell FTP on port 1337 and Vita Companion control
on port 1338.

See [scripts/prepare/README.md](scripts/prepare/README.md) for dependency details
and troubleshooting.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for commit conventions, tests and pull requests.

## License

Moonlight Vita is licensed under the GNU General Public License v3.0. See
[LICENSE](LICENSE).

## Credits

Moonlight Vita exists because of the work of the Moonlight community and the
developers who maintained the Vita port over many years. Major historical
contributors include:

- [Iwan Timmer](https://github.com/irtimmer), original Moonlight Embedded maintainer
- [xyzz](https://github.com/xyzz), creator and maintainer of the Vita port
- [d3m3vilurr](https://github.com/d3m3vilurr), long-term Vita maintenance and platform updates
- [Cameron Gutman](https://github.com/cgutman), Moonlight protocol and streaming work

Thanks also to [AorsiniYT](https://github.com/AorsiniYT), the
[Moonlight Vita contributors](https://github.com/xyzz/vita-moonlight/graphs/contributors)
and the maintainers of
[moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c).

The modern interface uses [Borealis](https://github.com/xfangfang/borealis), created and
maintained by Natinusala, xfangfang, XITRIX and its contributors. The toolchain
and platform libraries are provided by the [VitaSDK](https://vitasdk.org/)
community. The modern UI font uses a subset of
[Noto Sans CJK](https://github.com/notofonts/noto-cjk), while Legacy uses a
bundled mononoki Nerd Font. Thanks to the authors of these fonts and
[Material Icons](https://github.com/google/material-design-icons).

Thanks also to the developers of [Moonbeam](https://github.com/AorsiniYT/moonbeam),
[Moonmic](https://github.com/AorsiniYT/moonmic), [FFmpeg](https://ffmpeg.org/),
[ENet](https://github.com/cgutman/enet), [inih](https://github.com/benhoyt/inih),
[h264bitstream](https://github.com/aizvorski/h264bitstream), and
[libuuid](https://github.com/util-linux/util-linux), whose work supports the
Vita port. See the respective source repositories for licensing information.
