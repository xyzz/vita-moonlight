#!/usr/bin/env python3
"""Find recent signed snapshots containing the required VitaSDK packages."""

import hashlib
import io
import json
import os
import re
import subprocess
import sys
import tarfile
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path


CHANNEL_REPOSITORY = "vitasdk/vitasdk.github.io"


def download(url, api=False):
    headers = {"User-Agent": "moonlight-vita-sdk"}
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if api:
        headers["Accept"] = "application/vnd.github+json"
        if token:
            headers["Authorization"] = f"Bearer {token}"
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=30) as response:
        return response.read()


def package_names(database):
    names = set()
    with tarfile.open(fileobj=io.BytesIO(database), mode="r:*") as archive:
        for member in archive:
            if member.isfile() and Path(member.name).name == "desc":
                desc = archive.extractfile(member)
                if desc:
                    lines = desc.read().decode("utf-8", "replace").splitlines()
                    for index, line in enumerate(lines[:-1]):
                        if line == "%NAME%":
                            names.add(lines[index + 1])
                            break
    return names


def matches_installed_sdk(manifest, installed):
    core = manifest.get("core") or {}
    packages = manifest.get("packages") or {}
    database = packages.get("database") or {}
    installed_core = installed.get("core") or {}
    installed_packages = installed.get("packages") or {}
    installed_database = installed_packages.get("database") or {}
    release = packages.get("release", "")

    return (
        manifest.get("schema_version") == 1
        and manifest.get("channel") == installed.get("channel")
        and manifest.get("world") == installed.get("world")
        and core.get("release") == installed_core.get("release")
        and packages.get("repository") == installed_packages.get("repository")
        and database.get("name") == installed_database.get("name")
        and isinstance(release, str)
        and release.startswith(f"packages-{installed.get('channel')}-snapshot-")
        and re.fullmatch(r"[0-9a-fA-F]{64}", database.get("sha256", "")) is not None
    )


def native_path(path):
    if os.name == "nt" and path.startswith("/"):
        return subprocess.check_output(["cygpath", "-w", path], text=True).strip()
    return path


def native_process_argument(path):
    value = str(path)
    if sys.platform.startswith("cygwin"):
        return subprocess.check_output(["cygpath", "-w", value], text=True).strip()
    return value


def find_channel_verifier(sdk):
    candidates = (
        sdk / "share/vdpm/msys/usr/bin/vdpm-channel.exe",
        sdk / "bin/vdpm-channel",
        sdk / "bin/vdpm-channel.exe",
    )
    return next((candidate for candidate in candidates if candidate.is_file()), None)


def main():
    if len(sys.argv) < 3 or not os.environ.get("VITASDK"):
        print("Usage: select_vitasdk_snapshot.py OUTPUT_DIR PACKAGE... (with VITASDK set)", file=sys.stderr)
        return 2

    sdk = Path(native_path(os.environ["VITASDK"]))
    output = Path(native_path(sys.argv[1]))
    required = set(sys.argv[2:])
    output.mkdir(parents=True, exist_ok=True)
    installed = json.loads((sdk / "var/lib/vdpm/channel.json").read_text(encoding="utf-8"))
    channel = installed["channel"]
    if os.environ.get("VITASDK_CHANNEL", channel) != channel:
        raise ValueError("VITASDK_CHANNEL does not match the installed SDK")

    verifier = find_channel_verifier(sdk)
    public_key = sdk / "share/vdpm/channel-public-key.pem"
    if verifier is None:
        raise FileNotFoundError(f"VitaSDK channel verifier is missing under {sdk}")
    if not public_key.is_file():
        raise FileNotFoundError(f"VitaSDK channel public key is missing: {public_key}")

    api = f"https://api.github.com/repos/{CHANNEL_REPOSITORY}/commits?path=channels/{channel}.json&per_page=100"
    commits = json.loads(download(api, api=True))
    if not isinstance(commits, list):
        raise ValueError("GitHub returned an invalid channel history")
    candidates = output / "verify.json"
    signature = output / "verify.json.sig"
    seen = set()
    count = 0
    first_verifier_failure = None

    for commit in commits:
        if not isinstance(commit, dict):
            continue
        sha = commit.get("sha", "")
        if not re.fullmatch(r"[0-9a-f]{40}", sha):
            continue
        base = f"https://raw.githubusercontent.com/{CHANNEL_REPOSITORY}/{sha}/channels/{channel}.json"

        try:
            manifest_data = download(base)
            signature_data = download(base + ".sig")
            candidates.write_bytes(manifest_data)
            signature.write_bytes(signature_data)
            result = subprocess.run(
                [
                    str(verifier),
                    "verify",
                    native_process_argument(candidates),
                    native_process_argument(signature),
                    native_process_argument(public_key),
                ],
                capture_output=True,
                check=False,
            )
            if result.returncode:
                if first_verifier_failure is None:
                    details = (result.stderr or result.stdout).decode("utf-8", "replace").strip()
                    first_verifier_failure = (sha[:12], result.returncode, details)
                continue

            manifest = json.loads(manifest_data)
            if not isinstance(manifest, dict) or not matches_installed_sdk(manifest, installed):
                continue
            packages = manifest["packages"]
            release = packages["release"]
            if release in seen:
                continue
            seen.add(release)

            database_info = packages["database"]
            database_url = (
                f"https://github.com/{packages['repository']}/releases/download/"
                f"{urllib.parse.quote(release, safe='')}/{urllib.parse.quote(database_info['name'], safe='')}"
            )
            database = download(database_url)
            if hashlib.sha256(database).hexdigest() != database_info["sha256"].lower():
                continue

            missing = required - package_names(database)
            if missing:
                print(f"[snapshot] {release} is missing {', '.join(sorted(missing))}", file=sys.stderr)
                continue

            candidate = output / f"candidate-{count + 1}.json"
            candidate.write_bytes(manifest_data)
            Path(str(candidate) + ".sig").write_bytes(signature_data)
            print(f"{release}\t{candidate.resolve()}")
            print(f"[snapshot] Found {release}", file=sys.stderr)
            count += 1
            if count == 5:
                break
        except (urllib.error.URLError, OSError, ValueError, KeyError, TypeError, tarfile.TarError) as error:
            print(f"[snapshot] Skipping {sha[:12]}: {error}", file=sys.stderr)
        finally:
            candidates.unlink(missing_ok=True)
            signature.unlink(missing_ok=True)

    if not count:
        if first_verifier_failure:
            commit, exit_code, details = first_verifier_failure
            message = f"Channel signature verification failed for {commit} (exit {exit_code})"
            if details:
                message += f": {details}"
            print(f"[snapshot] {message}", file=sys.stderr)
        print(f"ERROR: No complete package snapshot found for VitaSDK {channel}.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, TypeError, urllib.error.URLError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
