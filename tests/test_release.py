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


if __name__ == "__main__":
    unittest.main()
