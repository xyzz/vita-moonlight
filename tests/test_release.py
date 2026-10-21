import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import release


class VersionTests(unittest.TestCase):
    def test_parse_version(self):
        self.assertEqual(release.parse_version("1.2.3"), (1, 2, 3))
        for value in ("1.2", "01.2.3", "1.2.x"):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    release.parse_version(value)

    def test_automatic_bump(self):
        self.assertEqual(release.automatic_bump([("fix: retry pairing", "")]), "patch")
        self.assertEqual(release.automatic_bump([("feat: add 480p", "")]), "minor")
        self.assertEqual(release.automatic_bump([("feat!: replace protocol", "")]), "major")
        self.assertEqual(
            release.automatic_bump([("fix: parse host", "BREAKING CHANGE: new format")]),
            "major",
        )

    def test_automatic_bump_rejects_non_conventional_subjects(self):
        for subject in ("Update pairing", "change: update pairing"):
            with self.subTest(subject=subject):
                with self.assertRaisesRegex(ValueError, "Conventional Commit"):
                    release.automatic_bump([(subject, "")])

    def test_validate_release_records_accepts_supported_types(self):
        release.validate_release_records(
            [
                ("fix(audio): retry playback", ""),
                ("docs: clarify setup", ""),
                ("feat(ui)!: replace navigation", ""),
            ]
        )

    def test_bump_version(self):
        self.assertEqual(release.bump_version((1, 2, 3), "patch"), (1, 2, 4))
        self.assertEqual(release.bump_version((1, 2, 3), "minor"), (1, 3, 0))
        self.assertEqual(release.bump_version((1, 2, 3), "major"), (2, 0, 0))


class ChangelogTests(unittest.TestCase):
    def test_repository_web_url_uses_github_environment(self):
        with mock.patch.dict(
            os.environ,
            {
                "GITHUB_REPOSITORY": "Example/Project",
                "GITHUB_SERVER_URL": "https://github.example",
            },
            clear=False,
        ):
            self.assertEqual(
                release.repository_web_url(),
                "https://github.example/Example/Project",
            )

    def test_repository_web_url_uses_origin_remote(self):
        with (
            mock.patch.dict(
                os.environ,
                {"GITHUB_REPOSITORY": "", "GITHUB_SERVER_URL": ""},
                clear=False,
            ),
            mock.patch(
                "release.git",
                return_value="git@github.com:Example/Project.git",
            ),
        ):
            self.assertEqual(
                release.repository_web_url(),
                "https://github.com/Example/Project",
            )

    @mock.patch("release.git")
    def test_commit_records_exclude_merges(self, git):
        git.return_value = "\x1efix: retry pairing\x1fbody"
        self.assertEqual(release.commit_records("1.0.0"), [("fix: retry pairing", "body")])
        git.assert_called_once_with(
            "log", "1.0.0..HEAD", "--no-merges", "--format=%x1e%s%x1f%b"
        )

    def test_release_note_groups_keep_changes_grouped_by_scope(self):
        groups = release.release_note_groups(
            [
                ("refactor(input): simplify shortcuts", ""),
                ("fix(input): preserve shortcuts", ""),
                ("fix(input): correct overlays", ""),
                ("feat(video): add presentation modes", ""),
                ("feat(input): add keyboard layouts", ""),
                ("feat(input): add touch zones", ""),
            ],
            "1.0.0",
        )

        rendered = dict(groups)
        major = dict(rendered["✨ Major Features"])
        core = dict(rendered["🔧 Core Improvements"])
        internal = dict(rendered["⚙️ Under the Hood"])

        self.assertEqual(
            major["Input"],
            ("Add touch zones", "Add keyboard layouts"),
        )
        self.assertEqual(major["Video"], ("Add presentation modes",))
        self.assertEqual(
            core["Input"],
            ("Correct overlays", "Preserve shortcuts"),
        )
        self.assertEqual(internal["Input"], ("Simplify shortcuts",))

    @mock.patch("release.commit_records")
    def test_changelog_uses_plain_list(self, records):
        records.return_value = [
            ("build: update VitaSDK package.", ""),
            ("refactor(video): share frame state", ""),
            ("fix(input): preserve uppercase text", ""),
            ("feat(stream): add 480p", ""),
        ]
        changelog = release.changelog_section("1.1.0", "1.0.0")
        self.assertEqual(
            changelog,
            "## 1.1.0\n\n"
            "- Add 480p\n"
            "- Preserve uppercase text\n"
            "- Share frame state\n"
            "- Update VitaSDK package\n\n",
        )
        self.assertNotIn("Full Changelog", changelog)

    def test_changeinfo_groups_patch_releases(self):
        changelog = "## 1.0.1\n\n- Fix A\n\n## 1.0.0\n\n- Add B\n"
        changeinfo = release.render_changeinfo(changelog)
        self.assertEqual(changeinfo.count('<changes app_ver="01.00">'), 1)
        self.assertIn("## 1.0.1", changeinfo)
        self.assertIn("## 1.0.0", changeinfo)

    def test_changeinfo_respects_size_limit(self):
        changelog = "## 1.0.0\n\n" + ("x" * 100)
        with mock.patch.object(release, "CHANGEINFO_LIMIT", 64):
            changeinfo = release.render_changeinfo(changelog)
        self.assertNotIn('<changes app_ver="01.00">', changeinfo)

    def test_release_notes_use_classic_release_style(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "CHANGELOG.md"
            path.write_text("## 1.0.0\n\n- Initial release\n", encoding="utf-8")
            previous = os.getcwd()
            try:
                os.chdir(tmp)
                with (
                    mock.patch(
                        "release.release_tags",
                        return_value=[((0, 13, 2), "0.13.2")],
                    ),
                    mock.patch(
                        "release.commit_records",
                        return_value=[
                            ("refactor(video): simplify decoder state", ""),
                            ("fix(input): preserve shortcuts", ""),
                            ("feat(ui): add quick settings", ""),
                        ],
                    ),
                    mock.patch(
                        "release.repository_web_url",
                        return_value="https://github.com/Example/Project",
                    ),
                    mock.patch("release.git", return_value="AorsiniYT\nAorsiniYT"),
                ):
                    notes = release.release_notes("1.0.0")
            finally:
                os.chdir(previous)

        self.assertIn("## What's Changed", notes)
        self.assertIn("### ✨ Major Features", notes)
        self.assertIn("### 🔧 Core Improvements", notes)
        self.assertIn("### ⚙️ Under the Hood", notes)
        self.assertIn("## Contributors\n\n- @AorsiniYT", notes)
        self.assertIn(
            "**Full Changelog**: https://github.com/Example/Project/compare/0.13.2...1.0.0",
            notes,
        )

    def test_release_notes_group_future_commits_by_type(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "CHANGELOG.md"
            path.write_text("## 1.0.1\n\n- Patch release\n", encoding="utf-8")
            previous = os.getcwd()
            try:
                os.chdir(tmp)
                with (
                    mock.patch(
                        "release.release_tags",
                        return_value=[((1, 0, 0), "1.0.0")],
                    ),
                    mock.patch(
                        "release.commit_records",
                        return_value=[
                            ("chore(release): prepare 1.0.1", ""),
                            ("refactor(video): simplify decoder state", ""),
                            ("fix(input): preserve shortcuts", ""),
                            ("feat(ui): add quick settings", ""),
                        ],
                    ),
                    mock.patch(
                        "release.repository_web_url",
                        return_value="https://github.com/Example/Project",
                    ),
                    mock.patch("release.git", return_value="AorsiniYT"),
                ):
                    notes = release.release_notes("1.0.1")
            finally:
                os.chdir(previous)

        self.assertIn("- **Add quick settings**", notes)
        self.assertIn("- **Preserve shortcuts**", notes)
        self.assertIn("- **Simplify decoder state**", notes)
        self.assertNotIn("Prepare 1.0.1", notes)
        self.assertIn("compare/1.0.0...1.0.1", notes)

    def test_release_notes_use_published_tag_as_target(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "CHANGELOG.md"
            path.write_text("## 1.0.1\n\n- Patch release\n", encoding="utf-8")
            previous = os.getcwd()
            try:
                os.chdir(tmp)
                with (
                    mock.patch(
                        "release.release_tags",
                        return_value=[
                            ((1, 0, 0), "1.0.0"),
                            ((1, 0, 1), "1.0.1"),
                        ],
                    ),
                    mock.patch(
                        "release.commit_records",
                        return_value=[("fix(input): preserve shortcuts", "")],
                    ) as records,
                    mock.patch(
                        "release.repository_web_url",
                        return_value="https://github.com/Example/Project",
                    ),
                    mock.patch("release.git", return_value="AorsiniYT") as git,
                ):
                    release.release_notes("1.0.1")
            finally:
                os.chdir(previous)

        records.assert_called_once_with("1.0.0", "1.0.1")
        git.assert_called_once_with(
            "log",
            "1.0.0..1.0.1",
            "--no-merges",
            "--format=%aN",
        )

    def test_changelog_baseline_uses_target_or_latest_previous_version(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "CHANGELOG.md"
            path.write_text(
                "## 1.0.0\n\n- Initial release\n\n"
                "## 0.13.2\n\n- Previous release\n",
                encoding="utf-8",
            )
            with mock.patch("release.git", return_value="target-baseline") as git:
                baseline = release.changelog_baseline_commit(path, "1.0.0")
            self.assertEqual(baseline, "target-baseline")
            git.assert_called_once_with(
                "log", "-1", "--format=%H", "-S", "## 1.0.0", "--", path.as_posix()
            )

            with mock.patch("release.git", return_value=""):
                with self.assertRaisesRegex(ValueError, "could not locate the Git commit"):
                    release.changelog_baseline_commit(path, "1.0.0")

            with mock.patch("release.git", return_value="previous-baseline") as git:
                baseline = release.changelog_baseline_commit(path, "1.0.1")
            self.assertEqual(baseline, "previous-baseline")
            git.assert_called_once_with(
                "log", "-1", "--format=%H", "-S", "## 1.0.0", "--", path.as_posix()
            )

    def test_later_baseline_selects_descendant_commit(self):
        with mock.patch(
            "release.git",
            side_effect=["first-commit", "second-commit", "first-commit"],
        ):
            self.assertEqual(
                release.later_baseline("first", "second"),
                "second-commit",
            )

        with mock.patch(
            "release.git",
            side_effect=["first-commit", "second-commit", "second-commit"],
        ):
            self.assertEqual(
                release.later_baseline("first", "second"),
                "first-commit",
            )

        with mock.patch(
            "release.git",
            side_effect=["first-commit", "second-commit", "common-commit"],
        ):
            with self.assertRaisesRegex(ValueError, "do not share a linear history"):
                release.later_baseline("first", "second")


class ComponentReleaseTests(unittest.TestCase):
    def test_commit_records_with_paths(self):
        raw = (
            "\x1efix(legacy): align welcome\x1f\x1d\n\nlegacy/src/gui/ui.c\n"
            "\x1efeat(ui): improve layout\x1fDetailed body\x1d\n\nmoonlight/src/ui.cpp\n"
        )
        with mock.patch("release.git", return_value=raw):
            records = release.commit_records_with_paths("1.0.0")
        self.assertEqual(records[0], ("fix(legacy): align welcome", "", ("legacy/src/gui/ui.c",)))
        legacy, borealis, shared = release.split_component_records(records)
        self.assertEqual(len(legacy), 1)
        self.assertEqual(len(borealis), 1)
        self.assertEqual(shared, [])

    def test_global_bumps_for_legacy_only_release(self):
        with (
            mock.patch("release.latest_tag", return_value=((1, 0, 0), "1.0.0")),
            mock.patch("release.commit_records", return_value=[("fix(legacy): align welcome", "")]),
        ):
            self.assertEqual(release.next_version("", "auto"), (1, 0, 1))

    def test_legacy_bump_depends_on_legacy_changes(self):
        current = (0, 13, 2)
        self.assertEqual(release.bump_legacy_only_if_changed(current, []), current)
        self.assertEqual(
            release.bump_legacy_only_if_changed(current, [("fix(legacy): align welcome", "")]),
            (0, 13, 3),
        )
        self.assertEqual(
            release.bump_legacy_only_if_changed(current, [("feat(legacy): add menu", "")]),
            (0, 14, 0),
        )

    def test_legacy_cmake_replacement_skips_commented_examples(self):
        original = '# set(VERSION_MINOR "11")\nset(VERSION_MINOR "13")\n'
        changed = release.replace_legacy_cmake_value(original, "VERSION_MINOR", 14)
        self.assertEqual(changed, '# set(VERSION_MINOR "11")\nset(VERSION_MINOR "14")\n')

    @staticmethod
    def build_release_fixture(root):
        root = Path(root)
        (root / "legacy").mkdir()
        (root / "resources").mkdir()
        for relative in release.LIVEAREA_PATHS:
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('<str color="#999999">v1.0.0</str>\n', encoding="utf-8")
        (root / "CMakeLists.txt").write_text(
            'set(VERSION_MAJOR "1")\nset(VERSION_MINOR "0")\n'
            'set(VERSION_ALTER "0")\nset(VERSION_BUILD "0")\n'
            'set(PSN_VERSION "01.00")\n', encoding="utf-8",
        )
        (root / "legacy/CMakeLists.txt").write_text(
            '# set(VERSION_MINOR "11")\nset(VERSION_MAJOR "0")\n'
            'set(VERSION_MINOR "13")\nset(VERSION_PATCH "2")\n',
            encoding="utf-8",
        )
        (root / "CHANGELOG.md").write_text(
            "## 1.0.0\n\n- Initial Borealis release\n",
            encoding="utf-8",
        )
        (root / "legacy/CHANGELOG.md").write_text(
            "## 0.13.2\n\n- Previous Legacy release\n",
            encoding="utf-8",
        )
        (root / "resources/changeinfo.xml").write_text("", encoding="utf-8")

    def test_legacy_only_update_is_idempotent(self):
        records = [("fix(legacy): align welcome", "", ("legacy/src/gui/ui.c",))]
        with tempfile.TemporaryDirectory() as tmp:
            self.build_release_fixture(tmp)
            previous = os.getcwd()
            try:
                os.chdir(tmp)
                with (
                    mock.patch("release.git", return_value="legacy-baseline"),
                    mock.patch("release.commit_records_with_paths", return_value=records),
                ):
                    release.update_files("1.0.1", "1.0.0")
                    release.verify_version("1.0.1")
                    first_changelog = Path("CHANGELOG.md").read_text(encoding="utf-8")
                    release.update_files("1.0.1", "1.0.0")
                self.assertEqual(release.configured_legacy_version(), (0, 13, 3))
                self.assertEqual(release.configured_version(), (1, 0, 1))
                self.assertEqual(Path("CHANGELOG.md").read_text(encoding="utf-8"), first_changelog)
                self.assertIn("### Moonlight Legacy v0.13.3", first_changelog)
                self.assertIn("### Moonlight Borealis v1.0.1", first_changelog)
                self.assertIn("## 0.13.3", Path("legacy/CHANGELOG.md").read_text(encoding="utf-8"))
            finally:
                os.chdir(previous)

    def test_borealis_only_keeps_legacy_version(self):
        records = [("fix(ui): improve playback", "", ("moonlight/src/player.cpp",))]
        with tempfile.TemporaryDirectory() as tmp:
            self.build_release_fixture(tmp)
            previous = os.getcwd()
            try:
                os.chdir(tmp)
                with (
                    mock.patch("release.git", return_value="legacy-baseline"),
                    mock.patch("release.commit_records_with_paths", return_value=records),
                ):
                    release.update_files("1.0.1", "1.0.0")
                    release.verify_version("1.0.1")
                self.assertEqual(release.configured_legacy_version(), (0, 13, 2))
                self.assertEqual(release.configured_version(), (1, 0, 1))
                self.assertIn(
                    "### Moonlight Legacy v0.13.2",
                    Path("CHANGELOG.md").read_text(encoding="utf-8"),
                )
            finally:
                os.chdir(previous)

    def test_existing_initial_release_notes_are_preserved(self):
        records = [("fix(legacy): align welcome", "", ("legacy/src/gui/ui.c",))]
        with tempfile.TemporaryDirectory() as tmp:
            self.build_release_fixture(tmp)
            previous = os.getcwd()
            try:
                os.chdir(tmp)
                def baseline_for(*args):
                    if args[0] == "rev-parse":
                        return args[1]
                    if args[0] == "merge-base":
                        return args[1]
                    if args[-1] == "CHANGELOG.md":
                        return "root-baseline"
                    return "legacy-baseline"

                with (
                    mock.patch("release.git", side_effect=baseline_for),
                    mock.patch("release.commit_records_with_paths", return_value=records),
                ):
                    release.update_files("1.0.0", "0.13.2")
                    release.update_files("1.0.0", "0.13.2")
                result = Path("CHANGELOG.md").read_text(encoding="utf-8")
                self.assertEqual(result.count("### Previous release notes"), 1)
                self.assertIn("- Initial Borealis release", result)
                self.assertEqual(release.configured_legacy_version(), (0, 13, 3))
            finally:
                os.chdir(previous)

    def test_initial_release_uses_existing_changelog_baselines(self):
        root_records = [
            (
                "chore(release): record Legacy baseline",
                "",
                ("legacy/CMakeLists.txt", "legacy/CHANGELOG.md"),
            ),
            ("feat(legacy): add menu", "", ("legacy/src/gui/ui.c",)),
            ("fix(ui): improve playback", "", ("moonlight/src/player.cpp",)),
        ]
        legacy_records = [
            ("feat(legacy): add menu", "", ("legacy/src/gui/ui.c",)),
        ]

        def baseline_for(*args):
            if args[0] == "rev-parse":
                return args[1]
            if args[0] == "merge-base":
                return args[1]
            if args[-1] == "CHANGELOG.md":
                return "root-baseline"
            if args[-1] == "legacy/CHANGELOG.md":
                return "legacy-baseline"
            raise AssertionError(f"unexpected git arguments: {args}")

        def records_for(revision):
            if revision == "root-baseline":
                return root_records
            if revision == "legacy-baseline":
                return legacy_records
            raise AssertionError(f"unexpected release baseline: {revision}")

        with tempfile.TemporaryDirectory() as tmp:
            self.build_release_fixture(tmp)
            previous = os.getcwd()
            try:
                os.chdir(tmp)
                with (
                    mock.patch("release.git", side_effect=baseline_for),
                    mock.patch("release.commit_records_with_paths", side_effect=records_for),
                ):
                    release.update_files("1.0.0", "")
                    release.verify_version("1.0.0")
                    first_changelog = Path("CHANGELOG.md").read_text(encoding="utf-8")
                    first_changeinfo = Path("resources/changeinfo.xml").read_text(encoding="utf-8")
                    release.update_files("1.0.0", "")

                self.assertEqual(release.configured_legacy_version(), (0, 14, 0))
                changelog = Path("CHANGELOG.md").read_text(encoding="utf-8")
                self.assertEqual(changelog, first_changelog)
                self.assertIn("### Moonlight Legacy v0.14.0", changelog)
                self.assertIn("### Moonlight Borealis v1.0.0", changelog)
                self.assertIn("- Add menu", changelog)
                self.assertIn("- Improve playback", changelog)
                self.assertNotIn("Record Legacy baseline", changelog)

                legacy_changelog = Path("legacy/CHANGELOG.md").read_text(encoding="utf-8")
                self.assertIn("## 0.14.0", legacy_changelog)
                self.assertIn("- Add menu", legacy_changelog)
                self.assertNotIn("Record Legacy baseline", legacy_changelog)

                changeinfo = Path("resources/changeinfo.xml").read_text(encoding="utf-8")
                self.assertEqual(changeinfo, first_changeinfo)
                self.assertIn("## 1.0.0", changeinfo)
                self.assertNotIn("Record Legacy baseline", changeinfo)
                self.assertLess(len(changeinfo.encode("utf-8")), 4096)
            finally:
                os.chdir(previous)

    def test_initial_release_uses_latest_legacy_baseline_after_previous_tag(self):
        root_records = [
            ("feat(legacy): add menu", "", ("legacy/src/gui/ui.c",)),
            ("fix(legacy): align welcome", "", ("legacy/src/gui/ui.c",)),
            ("fix(ui): improve playback", "", ("moonlight/src/player.cpp",)),
        ]
        legacy_records = [
            ("fix(legacy): align welcome", "", ("legacy/src/gui/ui.c",)),
        ]

        def baseline_for(*args):
            if args[0] == "rev-parse":
                return args[1]
            if args[0] == "merge-base":
                return args[1]
            if args[-1] == "CHANGELOG.md":
                return "root-baseline"
            if args[-1] == "legacy/CHANGELOG.md":
                return "legacy-baseline"
            raise AssertionError(f"unexpected git arguments: {args}")

        def records_for(revision):
            if revision == "root-baseline":
                return root_records
            if revision == "legacy-baseline":
                return legacy_records
            raise AssertionError(f"unexpected release baseline: {revision}")

        with tempfile.TemporaryDirectory() as tmp:
            self.build_release_fixture(tmp)
            previous = os.getcwd()
            try:
                os.chdir(tmp)
                with (
                    mock.patch("release.git", side_effect=baseline_for),
                    mock.patch("release.commit_records_with_paths", side_effect=records_for),
                ):
                    release.update_files("1.0.0", "0.13.2")
                    release.verify_version("1.0.0")
                    first_changelog = Path("CHANGELOG.md").read_text(encoding="utf-8")
                    release.update_files("1.0.0", "0.13.2")

                self.assertEqual(release.configured_legacy_version(), (0, 13, 3))
                self.assertEqual(
                    Path("CHANGELOG.md").read_text(encoding="utf-8"),
                    first_changelog,
                )
                self.assertIn("### Moonlight Legacy v0.13.3", first_changelog)
                self.assertIn("- Align welcome", first_changelog)
                self.assertNotIn("- Add menu", first_changelog)
                self.assertIn("### Moonlight Borealis v1.0.0", first_changelog)

                legacy_changelog = Path("legacy/CHANGELOG.md").read_text(encoding="utf-8")
                self.assertIn("## 0.13.3", legacy_changelog)
                self.assertIn("- Align welcome", legacy_changelog)
                self.assertNotIn("- Add menu", legacy_changelog)
            finally:
                os.chdir(previous)


if __name__ == "__main__":
    unittest.main()
