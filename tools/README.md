# Development tools

These utilities are not required to build or run Moonlight Vita.

## Vita diagnostics

`vita/dump_psp2core.sh` downloads the latest crash dump from VitaShell FTP and
passes it to `vita-parse-core`. It expects `ip_vita.txt`, a Vita build with
symbols and a local `reference/vita-parse-core` checkout. Set
`KEEP_PSP2DMP=1` to keep the downloaded dump.

`scripts/vita_locate_issue.sh` runs the dump helper and produces a more detailed
report under `dump-analysis/`.

## Legacy MinGW experiments

The files under `legacy/mingw/` were used while testing the old Windows desktop
build. They are kept for reference and are not part of the supported VitaSDK
build. Their dependency versions and MinGW setup may need changes on current
systems.

Use `makepsv` or `makefast` for PS Vita builds.
