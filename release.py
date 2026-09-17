#!/usr/bin/env python3

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path


if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")


VERSION_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
CONVENTIONAL_RE = re.compile(
    r"^(?P<type>[A-Za-z]+)(?:\((?P<scope>[^)]+)\))?(?P<breaking>!)?:\s*(?P<title>.+)$"
)
LIVEAREA_PATHS = (
    Path("psv/sce_sys/livearea/contents/template.xml"),
    Path("sce_sys/livearea/contents/template.xml"),
)
CHANGEINFO_LIMIT = 65536


def parse_version(value):
    match = VERSION_RE.fullmatch(value.strip())
    if not match:
        raise ValueError("version must use MAJOR.MINOR.PATCH")
    return tuple(int(part) for part in match.groups())


def version_text(version):
    return ".".join(str(part) for part in version)


def git(*args):
    result = subprocess.run(
        ["git", *args],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return result.stdout.strip()


def repository_web_url():
    repository = os.environ.get("GITHUB_REPOSITORY", "").strip("/")
    server_url = os.environ.get("GITHUB_SERVER_URL", "").rstrip("/")
    if repository and server_url:
        return f"{server_url}/{repository}"

    remote = git("config", "--get", "remote.origin.url").strip()
    if not remote:
        return ""

    remote = remote.removesuffix(".git").rstrip("/")
    if remote.startswith("git@"):
        host, separator, path = remote[4:].partition(":")
        return f"https://{host}/{path}" if separator and path else ""

    ssh_match = re.fullmatch(r"ssh://(?:[^@/]+@)?([^/]+)/(.+)", remote)
    if ssh_match:
        return f"https://{ssh_match.group(1)}/{ssh_match.group(2)}"

    return remote if re.match(r"https?://", remote) else ""


def release_tags():
    tags = []
    for tag in git("tag", "--list").splitlines():
        try:
            tags.append((parse_version(tag), tag))
        except ValueError:
            continue
    return sorted(tags)


def latest_tag():
    tags = release_tags()
    return tags[-1] if tags else (None, "")


def previous_release_tag(version):
    requested = parse_version(version)
    previous = [(value, tag) for value, tag in release_tags() if value < requested]
    return previous[-1][1] if previous else ""


def commit_records(previous_tag, target_ref="HEAD"):
    revision = f"{previous_tag}..{target_ref}" if previous_tag else target_ref
    output = git("log", revision, "--no-merges", "--format=%x1e%s%x1f%b")
    records = []
    for record in output.split("\x1e"):
        if not record.strip():
            continue
        subject, _, body = record.partition("\x1f")
        records.append((subject.strip(), body.strip()))
    return records


def automatic_bump(records):
    for subject, body in records:
        match = CONVENTIONAL_RE.match(subject)
        if (match and match.group("breaking")) or re.search(
            r"^BREAKING[ -]CHANGE:\s*", body, re.MULTILINE
        ):
            return "major"
    if any(
        (match := CONVENTIONAL_RE.match(subject))
        and match.group("type").lower() == "feat"
        for subject, _ in records
    ):
        return "minor"
    return "patch"


def bump_version(version, bump):
    major, minor, patch = version
    if bump == "major":
        return major + 1, 0, 0
    if bump == "minor":
        return major, minor + 1, 0
    return major, minor, patch + 1


def next_version(requested, bump):
    current, current_tag = latest_tag()
    records = commit_records(current_tag)

    if requested:
        candidate = parse_version(requested)
        if current is not None and candidate <= current:
            raise ValueError(
                f"requested version must be newer than {version_text(current)}"
            )
        return candidate

    if current is None or current < (1, 0, 0):
        return 1, 0, 0
    if not records:
        raise ValueError(f"no commits found after {current_tag}")

    selected_bump = automatic_bump(records) if bump == "auto" else bump
    return bump_version(current, selected_bump)


def replace_cmake_value(content, name, value):
    pattern = re.compile(rf"set\({re.escape(name)}\s+\"?[^\")]+\"?\)")
    updated, count = pattern.subn(f'set({name} "{value}")', content, count=1)
    if count != 1:
        raise ValueError(f"could not find {name} in CMakeLists.txt")
    return updated


def replace_livearea_version(content, version):
    pattern = re.compile(
        r'(<str\b[^>]*\bcolor="#999999"[^>]*>)v[^<]+(</str>)'
    )
    updated, count = pattern.subn(rf"\g<1>v{version}\g<2>", content, count=1)
    if count != 1:
        raise ValueError("could not find the LiveArea version label")
    return updated


def readable_title(subject):
    match = CONVENTIONAL_RE.match(subject)
    if not match:
        return subject
    title = match.group("title").strip()
    if title:
        title = title[0].upper() + title[1:]
    return title


def release_scope_label(scope):
    if not scope:
        return "General"
    parts = re.split(r"[-_/]+", scope)
    return " ".join(
        part.upper() if len(part) <= 3 else part.capitalize()
        for part in parts
        if part
    )


def release_note_groups(records, version):
    grouped = {
        "✨ Major Features": {},
        "🔧 Core Improvements": {},
        "⚙️ Under the Hood": {},
    }
    seen = set()

    for subject, _ in reversed(records):
        if subject == f"chore(release): prepare {version}":
            continue

        conventional = CONVENTIONAL_RE.fullmatch(subject)
        if conventional:
            commit_type = conventional.group("type").lower()
            if commit_type == "feat":
                heading = "✨ Major Features"
            elif commit_type in {"fix", "perf"}:
                heading = "🔧 Core Improvements"
            else:
                heading = "⚙️ Under the Hood"
            scope = release_scope_label(conventional.group("scope"))
        else:
            heading = "⚙️ Under the Hood"
            scope = "General"

        title = readable_title(subject)
        title_key = title.casefold()
        if title_key in seen:
            continue
        seen.add(title_key)
        grouped[heading].setdefault(scope, []).append(title)

    return tuple(
        (heading, tuple((scope, tuple(titles)) for scope, titles in scopes.items()))
        for heading, scopes in grouped.items()
        if scopes
    )


def changelog_section(version, previous_tag):
    lines = [f"## {version}", ""]
    lines.extend(
        f"- {readable_title(subject)}"
        for subject, _ in reversed(commit_records(previous_tag))
    )
    lines.append("")
    return "\n".join(lines) + "\n"


def vita_app_version(label):
    try:
        major, minor, _ = parse_version(label)
    except ValueError:
        return "00.00"
    if major > 99 or minor > 99:
        raise ValueError("Vita package versions support at most two digits per field")
    return f"{major:02d}.{minor:02d}"


def render_changeinfo(changelog):
    headings = list(re.finditer(r"^## (?P<label>.+?)\s*$", changelog, re.MULTILINE))
    groups = {}
    for index, heading in enumerate(headings):
        end = headings[index + 1].start() if index + 1 < len(headings) else len(changelog)
        section = changelog[heading.start() : end].strip()
        app_version = vita_app_version(heading.group("label"))
        groups.setdefault(app_version, []).append(section)

    header = '<?xml version="1.0" encoding="UTF-8"?>\n<changeinfo>\n'
    footer = "</changeinfo>\n"
    entries = []
    for app_version, sections in groups.items():
        body = "\n\n".join(sections).replace("]]>", "]]]]><![CDATA[>")
        body = body.replace("\n", "<br>\n")
        entry = (
            f'<changes app_ver="{app_version}"><![CDATA[\n'
            f"{body}\n"
            "]]></changes>\n"
        )
        candidate = header + "".join(entries) + entry + footer
        if len(candidate.encode("utf-8")) > CHANGEINFO_LIMIT:
            break
        entries.append(entry)
    return header + "".join(entries) + footer


def update_files(version, previous_tag):
    major, minor, patch = parse_version(version)
    if major > 99 or minor > 99:
        raise ValueError("Vita package versions support at most two digits per field")

    cmake_path = Path("CMakeLists.txt")
    original_cmake = cmake_path.read_text(encoding="utf-8")
    cmake = original_cmake
    cmake = replace_cmake_value(cmake, "VERSION_MAJOR", major)
    cmake = replace_cmake_value(cmake, "VERSION_MINOR", minor)
    cmake = replace_cmake_value(cmake, "VERSION_ALTER", patch)
    cmake = replace_cmake_value(cmake, "VERSION_BUILD", 0)
    cmake = replace_cmake_value(cmake, "PSN_VERSION", f"{major:02d}.{minor:02d}")
    if cmake != original_cmake:
        cmake_path.write_text(cmake, encoding="utf-8")

    for livearea_path in LIVEAREA_PATHS:
        original_livearea = livearea_path.read_text(encoding="utf-8")
        livearea = replace_livearea_version(original_livearea, version)
        if livearea != original_livearea:
            livearea_path.write_text(livearea, encoding="utf-8")

    changelog_path = Path("CHANGELOG.md")
    changelog = changelog_path.read_text(encoding="utf-8")
    if not re.search(rf"^## {re.escape(version)}\s*$", changelog, re.MULTILINE):
        changelog = changelog_section(version, previous_tag) + changelog.lstrip()
        changelog_path.write_text(changelog, encoding="utf-8")

    changeinfo_path = Path("resources/changeinfo.xml")
    changeinfo = render_changeinfo(changelog)
    if changeinfo != changeinfo_path.read_text(encoding="utf-8"):
        changeinfo_path.write_text(changeinfo, encoding="utf-8")


def configured_version():
    content = Path("CMakeLists.txt").read_text(encoding="utf-8")
    values = {}
    for name in ("VERSION_MAJOR", "VERSION_MINOR", "VERSION_ALTER"):
        match = re.search(rf"set\({name}\s+\"?([0-9]+)\"?\)", content)
        if not match:
            raise ValueError(f"could not find {name} in CMakeLists.txt")
        values[name] = int(match.group(1))
    return values["VERSION_MAJOR"], values["VERSION_MINOR"], values["VERSION_ALTER"]


def verify_version(version):
    expected = parse_version(version)
    actual = configured_version()
    if actual != expected:
        raise ValueError(
            f"CMake version is {version_text(actual)}, expected {version_text(expected)}"
        )
    changelog = Path("CHANGELOG.md").read_text(encoding="utf-8")
    if not re.search(rf"^## {re.escape(version)}\s*$", changelog, re.MULTILINE):
        raise ValueError(f"CHANGELOG.md has no {version} section")

    cmake = Path("CMakeLists.txt").read_text(encoding="utf-8")
    app_version = vita_app_version(version)
    if not re.search(
        rf'set\(PSN_VERSION\s+"{re.escape(app_version)}"\)', cmake
    ):
        raise ValueError(f"CMake PSN version is not {app_version}")

    for livearea_path in LIVEAREA_PATHS:
        livearea = livearea_path.read_text(encoding="utf-8")
        if f">v{version}</str>" not in livearea:
            raise ValueError(f"{livearea_path} does not show v{version}")

    changeinfo = Path("resources/changeinfo.xml").read_text(encoding="utf-8")
    if f'<changes app_ver="{app_version}">' not in changeinfo:
        raise ValueError(f"resources/changeinfo.xml has no {app_version} entry")


def release_notes(version):
    changelog = Path("CHANGELOG.md").read_text(encoding="utf-8")
    match = re.search(
        rf"^## {re.escape(version)}\s*\n(?P<body>.*?)(?=^##\s|\Z)",
        changelog,
        re.MULTILINE | re.DOTALL,
    )
    if not match:
        raise ValueError(f"CHANGELOG.md has no {version} section")
    previous_tag = previous_release_tag(version)
    target_ref = (
        version
        if any(tag == version for _, tag in release_tags())
        else "HEAD"
    )
    sections = release_note_groups(commit_records(previous_tag, target_ref), version)

    lines = ["## What's Changed", ""]
    for heading, scopes in sections:
        lines.extend((f"### {heading}", ""))
        for scope, titles in scopes:
            if len(titles) == 1:
                lines.append(f"- **{titles[0]}**")
                continue
            lines.append(f"- **{scope}**")
            lines.extend(f"  - {title}" for title in titles)
        lines.append("")

    contributor_revision = (
        f"{previous_tag}..{target_ref}" if previous_tag else target_ref
    )
    contributors = git(
        "log",
        contributor_revision,
        "--no-merges",
        "--format=%aN",
    ).splitlines()
    contributor_lines = []
    for name in contributors:
        name = name.strip()
        if not name or name in contributor_lines or name.endswith("[bot]"):
            continue
        contributor_lines.append(name)
    if contributor_lines:
        lines.extend(("## Contributors", ""))
        for name in contributor_lines:
            rendered = f"@{name}" if re.fullmatch(r"[A-Za-z0-9-]+", name) else name
            lines.append(f"- {rendered}")
        lines.append("")

    repository_url = repository_web_url()
    if previous_tag and repository_url:
        lines.append(
            f"**Full Changelog**: {repository_url}/compare/{previous_tag}...{version}"
        )
        lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Moonlight Vita release helper")
    subparsers = parser.add_subparsers(dest="command", required=True)

    next_parser = subparsers.add_parser("next", help="calculate the next version")
    next_parser.add_argument("--requested", default="")
    next_parser.add_argument(
        "--bump", choices=("auto", "patch", "minor", "major"), default="auto"
    )

    update_parser = subparsers.add_parser("update", help="update release files")
    update_parser.add_argument("version")
    update_parser.add_argument("--previous", default="")

    verify_parser = subparsers.add_parser("verify", help="verify release files")
    verify_parser.add_argument("version")

    notes_parser = subparsers.add_parser("notes", help="print release notes")
    notes_parser.add_argument("version")

    subparsers.add_parser("latest", help="print the latest stable release tag")

    args = parser.parse_args()
    try:
        if args.command == "next":
            print(version_text(next_version(args.requested.strip(), args.bump)))
        elif args.command == "update":
            update_files(args.version, args.previous)
        elif args.command == "verify":
            verify_version(args.version)
        elif args.command == "notes":
            sys.stdout.write(release_notes(args.version))
        elif args.command == "latest":
            _, tag = latest_tag()
            print(tag)
    except (ValueError, subprocess.CalledProcessError) as error:
        print(f"release.py: {error}", file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
