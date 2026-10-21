#!/usr/bin/env python3

import argparse
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")


VERSION_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
CONVENTIONAL_RE = re.compile(
    r"^(?P<type>[A-Za-z]+)(?:\((?P<scope>[^)]+)\))?(?P<breaking>!)?:\s*(?P<title>.+)$"
)
VALID_COMMIT_TYPES = {
    "feat",
    "fix",
    "perf",
    "refactor",
    "build",
    "ci",
    "docs",
    "test",
    "chore",
    "revert",
}
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


def commit_records_with_paths(previous_tag):
    """Return (subject, body, changed paths) for each non-merge commit."""
    revision = f"{previous_tag}..HEAD" if previous_tag else "HEAD"
    output = git(
        "log", revision, "--no-merges", "--name-only",
        "--format=%x1e%s%x1f%b%x1d",
    )
    records = []
    for item in output.split("\x1e"):
        if not item.strip():
            continue
        metadata, separator, path_text = item.partition("\x1d")
        if not separator:
            raise ValueError("could not parse Git commit paths")
        subject, separator, body = metadata.partition("\x1f")
        if not separator:
            raise ValueError("could not parse Git commit message")
        paths = tuple(line.strip() for line in path_text.splitlines() if line.strip())
        records.append((subject.strip(), body.strip(), paths))
    return records


def changelog_baseline_commit(changelog_path, version):
    changelog_path = Path(changelog_path)
    changelog = changelog_path.read_text(encoding="utf-8")
    headings = [
        (parse_version(label), label)
        for label in re.findall(
            r"^## ([0-9]+\.[0-9]+\.[0-9]+)\s*$",
            changelog,
            re.MULTILINE,
        )
    ]
    if not headings:
        return ""

    requested = parse_version(version)
    selected = next(
        (label for value, label in reversed(headings) if value == requested),
        None,
    )
    if selected is None:
        previous = [item for item in headings if item[0] < requested]
        if not previous:
            return ""
        selected = max(previous)[1]

    baseline = git(
        "log",
        "-1",
        "--format=%H",
        "-S",
        f"## {selected}",
        "--",
        changelog_path.as_posix(),
    )
    if not baseline:
        raise ValueError(
            f"could not locate the Git commit that introduced {selected} "
            f"in {changelog_path.as_posix()}"
        )
    return baseline


def later_baseline(first, second):
    if not first:
        return second
    if not second:
        return first

    first_commit = git("rev-parse", first)
    second_commit = git("rev-parse", second)
    common_baseline = git("merge-base", first_commit, second_commit)
    if common_baseline == first_commit:
        return second_commit
    if common_baseline == second_commit:
        return first_commit
    raise ValueError("release baselines do not share a linear history")


def is_legacy_path(path):
    return path.startswith("legacy/") and path != "legacy/CHANGELOG.md"


def is_borealis_path(path):
    return path.startswith(("moonlight/", "patches/borealis/", "resources/"))


def split_component_records(records):
    """Mixed commits can affect multiple components without losing shared changes."""
    legacy, borealis, shared = [], [], []
    for subject, body, paths in records:
        entry = (subject, body)
        if any(is_legacy_path(path) for path in paths):
            legacy.append(entry)
        if any(is_borealis_path(path) for path in paths):
            borealis.append(entry)
        if not paths or any(
            not is_legacy_path(path)
            and not is_borealis_path(path)
            and path != "legacy/CHANGELOG.md"
            for path in paths
        ):
            shared.append(entry)
    return legacy, borealis, shared


def configured_legacy_version():
    content = Path("legacy/CMakeLists.txt").read_text(encoding="utf-8")
    values = []
    for name in ("VERSION_MAJOR", "VERSION_MINOR", "VERSION_PATCH"):
        match = re.search(rf'^set\({name}\s+"?([0-9]+)"?\)', content, re.MULTILINE)
        if not match:
            raise ValueError(f"could not find Legacy {name}")
        values.append(int(match.group(1)))
    return tuple(values)


def replace_legacy_cmake_value(content, name, value):
    pattern = re.compile(rf'^set\({re.escape(name)}\s+"?[0-9]+"?\)', re.MULTILINE)
    updated, count = pattern.subn(f'set({name} "{value}")', content, count=1)
    if count != 1:
        raise ValueError(f"could not update Legacy {name}")
    return updated


def bump_legacy_only_if_changed(current, records):
    return bump_version(current, automatic_bump(records)) if records else current


def component_changelog_section(version, legacy_version, records, legacy_records=None):
    legacy, borealis, shared = split_component_records(records)
    if legacy_records is not None:
        legacy = legacy_records
    lines = [f"## {version}", ""]
    for heading, entries, empty_text in (
        (f"Moonlight Legacy v{legacy_version}", legacy, "No Legacy-specific changes."),
        (f"Moonlight Borealis v{version}", borealis, "No Borealis-specific changes."),
        ("Shared", shared, "No shared changes."),
    ):
        lines.extend((f"### {heading}", ""))
        lines.extend(
            (f"- {readable_title(subject)}" for subject, _ in reversed(entries))
            if entries else (f"- {empty_text}",)
        )
        lines.append("")
    return "\n".join(lines) + "\n"


def legacy_changelog_section(version, records):
    lines = [f"## {version}", ""]
    lines.extend(f"- {readable_title(subject)}" for subject, _ in reversed(records))
    lines.append("")
    return "\n".join(lines) + "\n"


def version_section(changelog, version):
    return re.search(
        rf"^## {re.escape(version)}\s*\n(?P<body>.*?)(?=^##\s|\Z)",
        changelog, re.MULTILINE | re.DOTALL,
    )


def declared_legacy_version(section_body):
    match = re.search(
        r"^### Moonlight Legacy v([0-9]+\.[0-9]+\.[0-9]+)\s*$",
        section_body, re.MULTILINE,
    )
    return parse_version(match.group(1)) if match else None


def validate_release_records(records):
    invalid_subjects = []
    for subject, _ in records:
        match = CONVENTIONAL_RE.fullmatch(subject)
        if not match or match.group("type").lower() not in VALID_COMMIT_TYPES:
            invalid_subjects.append(subject)
    if invalid_subjects:
        joined = ", ".join(repr(subject) for subject in invalid_subjects)
        raise ValueError(
            "release commits must use Conventional Commit subjects; "
            f"invalid: {joined}"
        )


def automatic_bump(records):
    validate_release_records(records)

    for subject, body in records:
        match = CONVENTIONAL_RE.fullmatch(subject)
        if (match and match.group("breaking")) or re.search(
            r"^BREAKING[ -]CHANGE:\s*", body, re.MULTILINE
        ):
            return "major"
    if any(
        (match := CONVENTIONAL_RE.fullmatch(subject))
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

    validate_release_records(records)
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
    title = match.group("title").strip().rstrip(".")
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

    changelog_path = Path("CHANGELOG.md")
    changelog = changelog_path.read_text(encoding="utf-8")
    current_section = version_section(changelog, version)
    previously_prepared = (
        declared_legacy_version(current_section.group("body"))
        if current_section else None
    )
    current_legacy = configured_legacy_version()
    release_baseline = later_baseline(
        previous_tag,
        changelog_baseline_commit(changelog_path, version),
    )
    records = commit_records_with_paths(release_baseline)

    legacy_changelog_path = Path("legacy/CHANGELOG.md")
    if previously_prepared is not None:
        legacy_records = []
    else:
        legacy_baseline = changelog_baseline_commit(
            legacy_changelog_path,
            version_text(current_legacy),
        ) if legacy_changelog_path.exists() else ""
        legacy_baseline = later_baseline(release_baseline, legacy_baseline)
        legacy_source_records = (
            commit_records_with_paths(legacy_baseline)
            if legacy_baseline else records
        )
        legacy_records, _, _ = split_component_records(legacy_source_records)

    next_legacy = (
        previously_prepared if previously_prepared is not None
        else bump_legacy_only_if_changed(current_legacy, legacy_records)
    )
    legacy_version = version_text(next_legacy)

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

    if legacy_records:
        legacy_cmake_path = Path("legacy/CMakeLists.txt")
        original_legacy_cmake = legacy_cmake_path.read_text(encoding="utf-8")
        legacy_cmake = original_legacy_cmake
        for name, value in zip(
            ("VERSION_MAJOR", "VERSION_MINOR", "VERSION_PATCH"), next_legacy
        ):
            legacy_cmake = replace_legacy_cmake_value(legacy_cmake, name, value)
        if legacy_cmake != original_legacy_cmake:
            legacy_cmake_path.write_text(legacy_cmake, encoding="utf-8")

        legacy_changelog = legacy_changelog_path.read_text(encoding="utf-8")
        if not version_section(legacy_changelog, legacy_version):
            legacy_changelog = (
                legacy_changelog_section(legacy_version, legacy_records)
                + legacy_changelog.lstrip()
            )
            legacy_changelog_path.write_text(legacy_changelog, encoding="utf-8")

    for livearea_path in LIVEAREA_PATHS:
        original_livearea = livearea_path.read_text(encoding="utf-8")
        livearea = replace_livearea_version(original_livearea, version)
        if livearea != original_livearea:
            livearea_path.write_text(livearea, encoding="utf-8")

    if current_section is None:
        changelog = (
            component_changelog_section(
                version,
                legacy_version,
                records,
                legacy_records,
            )
            + changelog.lstrip()
        )
        changelog_path.write_text(changelog, encoding="utf-8")
    elif previously_prepared is None:
        # 1.0.0 already has curated release notes. Preserve those notes while
        # introducing per-component metadata for the initial combined release.
        preserved = current_section.group("body").strip()
        addition = component_changelog_section(
            version,
            legacy_version,
            records,
            legacy_records,
        )
        if preserved:
            addition += "### Previous release notes\n\n" + preserved + "\n\n"
        changelog = (
            changelog[:current_section.start()]
            + addition
            + changelog[current_section.end():]
        )
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

    section = version_section(changelog, version)
    expected_legacy = declared_legacy_version(section.group("body")) if section else None
    if expected_legacy is not None:
        if configured_legacy_version() != expected_legacy:
            raise ValueError("Legacy CMake version does not match release notes")
        legacy_changelog = Path("legacy/CHANGELOG.md").read_text(encoding="utf-8")
        if not version_section(legacy_changelog, version_text(expected_legacy)):
            raise ValueError("legacy/CHANGELOG.md lacks the Legacy release version")

    changeinfo = Path("resources/changeinfo.xml").read_text(encoding="utf-8")
    if len(changeinfo.encode("utf-8")) > CHANGEINFO_LIMIT:
        raise ValueError("resources/changeinfo.xml exceeds the Vita LiveArea size limit")
    try:
        changeinfo_root = ET.fromstring(changeinfo)
    except ET.ParseError as error:
        raise ValueError("resources/changeinfo.xml is not valid XML") from error
    current_entry = next(
        (
            entry for entry in changeinfo_root.findall("changes")
            if entry.get("app_ver") == app_version
        ),
        None,
    )
    if current_entry is None or f"## {version}" not in (current_entry.text or ""):
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
