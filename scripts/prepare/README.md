# Prepare scripts — building Vita dependencies

This folder contains helper packaging manifests and scripts used to build
and install PS Vita libraries required by the project: `mbedtls`, `ffmpeg`,
`sdl2` and `curl`.

There are two main entry points you can use depending on your environment:

- `scripts/prepare/install_all.sh` — a convenience script that runs
  `vita-makepkg` and installs the produced `*-arm.tar.xz` packages with
  `vdpm`. It is intended to be run inside the same environment used by the
  repository Dockerfiles (see below), or on a host that already has
  `vita-makepkg`, `vdpm` and a correctly configured `$VITASDK`.

- Dockerfiles: `scripts/prepare/Dockerfile` and `scripts/prepare/gxm.Dockerfile`
  reproduce the packaging environment used during development. Running the
  container created from those Dockerfiles is the recommended way to build
  packages reproducibly.

Prerequisites
-------------

- A VITASDK installation. The `VITASDK` environment variable must point to
  the toolchain root (for example `/usr/local/vitasdk`).
- `vita-makepkg` available in PATH and `vdpm` available in PATH (both are
  included in the Dockerfiles used here).
- Sufficient disk space and build toolchain (cmake, make, patch, etc.)

Quick usage (host)
-------------------

This runs the small orchestrator script on the host. It expects `VITASDK`,
`vita-makepkg` and `vdpm` to be present in your environment.

```bash
export VITASDK=/path/to/your/vitasdk
cd /path/to/vita-moonlight
scripts/prepare/install_all.sh --curl-backend=mbedtls
```

By default the script builds `curl` with the `mbedtls` backend (this matches
the default configuration used for `ffmpeg` in this repository). If you
need `curl` compiled against OpenSSL instead, pass `--curl-backend=openssl`:

```bash
scripts/prepare/install_all.sh --curl-backend=openssl
```

This will create a temporary copy of the `scripts/prepare/curl` package,
modify the `VITABUILD` to enable OpenSSL and then build and install the
resulting package.

Using Docker (recommended)
--------------------------

The included Dockerfiles provide an environment with `vita-makepkg`, `vdpm`
and the necessary build tools. A simple workflow is:

```bash
# build the image (uses the default Dockerfile)
docker build -t vita-builder -f scripts/prepare/Dockerfile .

# run a shell in the container and share the repo (the container's entrypoint
# will run commands you provide). This mounts the current repository into /src
docker run --rm -it -v "$(pwd)":/src vita-builder \
  "/src/scripts/prepare/install_all.sh --curl-backend=mbedtls"
```

If you need the GXM-specific packages (GXM renderer), use `gxm.Dockerfile` to
build the image and run the same command inside it.

Notes and troubleshooting
-------------------------

- If `vita-makepkg` fails, inspect the package folder (`scripts/prepare/<pkg>`)
  and run `vita-makepkg` manually to view full logs.
- Building with OpenSSL may require OpenSSL static archives or headers to be
  discoverable by the Vita toolchain; ensure your environment or the Docker
  image contains the required OpenSSL artifacts.
- The script tries to run `./install_psv2` (if present in the repository root)
  to install PVR/PVR_PSP2 stubs required by some SDL2 builds. If you rely on
  GXM or PVR, keep `install_psv2` available and executable.

If anything fails or you want the script to be more automated for your
specific environment (for example apply more VITABUILD edits or pass custom
cmake flags), open an issue/PR or ask here and I can adjust the script.

License: project license applies.
