"""Host test for .github/workflow_data/release.py: release notes show only the
CHANGELOG.md section of the tag being released, -rc tags are pre-releases, and
SHA256SUMS lists the release files.
  python3 -I tests/host/release_notes/test_release_notes.py   (or run.sh)
"""

import importlib.util
import hashlib
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))

spec = importlib.util.spec_from_file_location(
    "release", os.path.join(ROOT, ".github", "workflow_data", "release.py")
)
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)

SAMPLE = """# Changelog

Intro text that is never part of a release.

## Unreleased

### Added
- Not released yet

## flpw-002 - 2027-03-01

### Fixed
- Two

## flpw-001 - 2027-01-15

### Added
- One
- One more

## flpw-0010 - 2028-01-01

### Added
- Ten
"""


class ExtractSection(unittest.TestCase):
    def test_only_the_tags_section(self):
        out = release.extract_section(SAMPLE, "flpw-001")
        self.assertEqual(out, "### Added\n- One\n- One more")

    def test_first_and_last_sections(self):
        self.assertEqual(
            release.extract_section(SAMPLE, "flpw-002"), "### Fixed\n- Two"
        )
        self.assertEqual(
            release.extract_section(SAMPLE, "flpw-0010"), "### Added\n- Ten"
        )

    def test_tag_is_not_a_prefix_match(self):
        self.assertIsNone(release.extract_section(SAMPLE, "flpw-00"))
        self.assertIsNone(release.extract_section(SAMPLE, "flpw-003"))

    def test_unreleased_is_not_picked_for_a_tag(self):
        self.assertNotIn(
            "Not released yet", release.extract_section(SAMPLE, "flpw-002")
        )

    def test_bracketed_heading_and_bare_heading(self):
        self.assertEqual(
            release.extract_section("## [flpw-001] - x\n- a\n", "flpw-001"), "- a"
        )
        self.assertEqual(
            release.extract_section("## flpw-001\n- a\n## flpw-000\n- b", "flpw-001"),
            "- a",
        )

    def test_empty_section_is_missing(self):
        self.assertIsNone(
            release.extract_section("## flpw-001\n\n## flpw-000\n- b\n", "flpw-001")
        )

    def test_real_changelog(self):
        with open(os.path.join(ROOT, "CHANGELOG.md")) as f:
            text = f.read()
        # Unreleased is empty right after a release, so check a released section.
        self.assertRegex(text, r"(?m)^## Unreleased$")
        out = release.extract_section(text, "flpw-001")
        self.assertIsNotNone(out)
        self.assertIn("### Added", out)
        self.assertNotIn("Momentum's unreleased changelog", out)


class Tags(unittest.TestCase):
    def test_release_tags(self):
        self.assertEqual(release.parse_tag("flpw-001"), ("flpw-001", False))
        self.assertEqual(release.parse_tag("flpw-001-rc1"), ("flpw-001", True))
        self.assertEqual(release.parse_tag("flpw-12-rc10"), ("flpw-12", True))

    def test_not_release_tags(self):
        for tag in (
            "",
            "flpw-",
            "flpw-001-beta",
            "flpw-001-rc",
            "v1",
            "flpw-001rc1",
            "flpw-001-rc1x",
            "xflpw-001",
            "flpw-001\n",
        ):
            self.assertIsNone(release.parse_tag(tag), tag)

    def test_rc_is_prerelease(self):
        self.assertTrue(release.is_prerelease("flpw-001-rc2"))
        self.assertFalse(release.is_prerelease("flpw-001"))
        self.assertFalse(release.is_prerelease("whatever"))


class NotesBody(unittest.TestCase):
    def test_final_tag_needs_its_own_section(self):
        self.assertIsNone(release.release_notes_body(SAMPLE, "flpw-003"))
        # Unreleased is never used for a final release.
        self.assertIsNone(
            release.release_notes_body("## Unreleased\n- x\n", "flpw-001")
        )

    def test_rc_prefers_its_own_then_final_then_unreleased(self):
        own = "## Unreleased\n- u\n## flpw-001 - d\n- f\n## flpw-001-rc1 - d\n- r\n"
        self.assertEqual(release.release_notes_body(own, "flpw-001-rc1"), "- r")
        self.assertEqual(release.release_notes_body(own, "flpw-001-rc2"), "- f")
        self.assertEqual(
            release.release_notes_body("## Unreleased\n- u\n", "flpw-001-rc1"), "- u"
        )
        self.assertIsNone(release.release_notes_body("# nothing\n", "flpw-001-rc1"))

    def test_render_uses_flipware_links_only(self):
        with open(os.path.join(ROOT, ".github", "workflow_data", "release.md")) as f:
            template = f.read()
        final = release.render_notes(template, "flpw-001", "- changes {with braces}")
        self.assertIn(
            "releases/download/flpw-001/flipper-z-f7-update-flpw-001.tgz", final
        )
        self.assertIn(
            "releases/download/flpw-001/flipper-z-f7-update-flpw-001.zip", final
        )
        self.assertIn(
            "releases/download/flpw-001/flipper-z-f7-full-flpw-001.dfu", final
        )
        self.assertIn("releases/download/flpw-001/flipper-z-f7-sdk-flpw-001.zip", final)
        self.assertIn("releases/download/flpw-001/SHA256SUMS", final)
        self.assertIn("- changes {with braces}", final)
        self.assertNotIn("Release candidate", final)
        self.assertNotIn("{", final.replace("{with braces}", ""))
        rc = release.render_notes(template, "flpw-001-rc1", "- x")
        self.assertTrue(rc.startswith("> **Release candidate.**"))
        for text in (final, rc):
            for urlpart in text.split("https://")[1:]:
                self.assertTrue(
                    urlpart.startswith("github.com/calvingunther66/Flipware"),
                    urlpart[:60],
                )
            for banned in (
                "Next-Flip",
                "Momentum",
                "discord",
                "patreon",
                "ko-fi",
                "sponsor",
            ):
                self.assertNotIn(banned.lower(), text.lower())


class Sums(unittest.TestCase):
    def test_sha256sums_format_and_order(self):
        with tempfile.TemporaryDirectory() as d:
            paths = []
            for name, data in (("b.zip", b"bbb"), ("a.dfu", b"a" * 3_000_000)):
                path = os.path.join(d, name)
                with open(path, "wb") as f:
                    f.write(data)
                paths.append(path)
            out = release.sha256sums(paths)
            expect = "".join(
                f"{hashlib.sha256(data).hexdigest()}  {name}\n"
                for name, data in (("a.dfu", b"a" * 3_000_000), ("b.zip", b"bbb"))
            )
            self.assertEqual(out, expect)

    def test_command_line(self):
        script = os.path.join(ROOT, ".github", "workflow_data", "release.py")
        with tempfile.TemporaryDirectory() as d:
            payload = os.path.join(d, "x.tgz")
            with open(payload, "wb") as f:
                f.write(b"hello")
            sums = os.path.join(d, "SHA256SUMS")
            subprocess.run(
                [sys.executable, "-I", script, "sums", "--out", sums, payload],
                check=True,
            )
            with open(sums) as f:
                self.assertEqual(
                    f.read(), hashlib.sha256(b"hello").hexdigest() + "  x.tgz\n"
                )
            self.assertEqual(
                subprocess.run(
                    [sys.executable, "-I", script, "is-prerelease", "flpw-001-rc1"],
                    capture_output=True,
                    text=True,
                ).stdout.strip(),
                "true",
            )
            self.assertNotEqual(
                subprocess.run(
                    [sys.executable, "-I", script, "check-tag", "v1"],
                    capture_output=True,
                ).returncode,
                0,
            )
            self.assertEqual(
                subprocess.run(
                    [sys.executable, "-I", script, "check-tag", "flpw-001"],
                    capture_output=True,
                ).returncode,
                0,
            )

    def test_notes_command_fails_without_section(self):
        script = os.path.join(ROOT, ".github", "workflow_data", "release.py")
        with tempfile.TemporaryDirectory() as d:
            log = os.path.join(d, "CHANGELOG.md")
            with open(log, "w") as f:
                f.write("## Unreleased\n- x\n")
            out = os.path.join(d, "notes.md")
            tpl = os.path.join(ROOT, ".github", "workflow_data", "release.md")
            cmd = [
                sys.executable,
                "-I",
                script,
                "notes",
                "--changelog",
                log,
                "--template",
                tpl,
                "--out",
                out,
                "--tag",
            ]
            result = subprocess.run(cmd + ["flpw-001"], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("no section", result.stderr)
            self.assertFalse(os.path.exists(out))
            self.assertEqual(
                subprocess.run(cmd + ["flpw-001-rc1"], capture_output=True).returncode,
                0,
            )
            with open(out) as f:
                self.assertIn("- x", f.read())


if __name__ == "__main__":
    unittest.main()
