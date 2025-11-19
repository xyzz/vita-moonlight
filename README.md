![Moonlight Vita Logo](resources/img/icon.jpg)

# Moonlight Vita

Moonlight client for PS Vita and PSTV, based on Moonlight Embedded. This branch
uses Borealis for the interface and keeps the Vita streaming backend in C++.

## Requirements

- A PS Vita or PSTV with homebrew support
- VitaShell to install the VPK
- A PC running Sunshine or another compatible GameStream host

## Build

Install VitaSDK, clone the repository with its submodules, then run the Vita
build script:

```bash
git clone --recurse-submodules https://github.com/xyzz/vita-moonlight.git
cd vita-moonlight
./makepsv
```

The generated VPK is written to the Vita build directory. The build script can
also deploy it when `ip_vita.txt` contains the address of a Vita running
VitaShell FTP.

## License

Moonlight Vita is licensed under the GNU General Public License v3.0. See
[LICENSE](LICENSE).

## Credits

Moonlight Vita builds on Moonlight Embedded and the work of the Moonlight Vita,
Borealis and VitaSDK contributors.
