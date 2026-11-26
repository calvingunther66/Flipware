"""Host test for .github/workflow_data/release.py: release notes show only the
CHANGELOG.md section of the tag being released.
  python3 -I tests/host/release_notes/test_release_notes.py   (or run.sh)
"""
import importlib.util
import os
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
        self.assertEqual(release.extract_section(SAMPLE, "flpw-002"), "### Fixed\n- Two")
        self.assertEqual(release.extract_section(SAMPLE, "flpw-0010"), "### Added\n- Ten")

    def test_tag_is_not_a_prefix_match(self):
        self.assertIsNone(release.extract_section(SAMPLE, "flpw-00"))
        self.assertIsNone(release.extract_section(SAMPLE, "flpw-003"))

    def test_unreleased_is_not_picked_for_a_tag(self):
        self.assertNotIn("Not released yet", release.extract_section(SAMPLE, "flpw-002"))

    def test_bracketed_heading_and_bare_heading(self):
        self.assertEqual(release.extract_section("## [flpw-001] - x\n- a\n", "flpw-001"), "- a")
        self.assertEqual(release.extract_section("## flpw-001\n- a\n## flpw-000\n- b", "flpw-001"), "- a")

    def test_empty_section_is_missing(self):
        self.assertIsNone(release.extract_section("## flpw-001\n\n## flpw-000\n- b\n", "flpw-001"))

    def test_real_changelog_has_unreleased(self):
        with open(os.path.join(ROOT, "CHANGELOG.md")) as f:
            text = f.read()
        out = release.extract_section(text, "Unreleased")
        self.assertIsNotNone(out)
        self.assertIn("### Added", out)
        self.assertNotIn("Momentum's unreleased changelog", out)


if __name__ == "__main__":
    unittest.main()
