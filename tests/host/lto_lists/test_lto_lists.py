"""The lists in scripts/fbt/lto.py have to match the source tree, or a build with
link-time optimization breaks (roadmap 2.2, documentation/LinkTimeOptimization.md):

  - every library an app links in (`fap_libs`) is in NO_LTO_LIBS, or the app link fails
    with "incremental linking of LTO and non-LTO objects";
  - WRAPPERS is exactly the set of `__wrap_<name>` functions the firmware defines, or
    the link fails with "undefined reference" (or a replacement is dropped);
  - NO_LTO_SOURCES names files that exist.
  python3 -I tests/host/lto_lists/test_lto_lists.py
"""

import os
import re
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from fbt import lto  # noqa: E402

WRAPPER_SOURCES = ("lib/print/wrappers.c", "furi/core/memmgr.c")
SKIP_DIRS = {".git", "build", "dist", "node_modules"}


def fap_libs():
    """Library names from every `fap_libs=[...]` in an application.fam, with the
    file that names them."""
    found = {}
    pattern = re.compile(r"fap_libs\s*=\s*\[([^\]]*)\]")
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        if "application.fam" not in filenames:
            continue
        path = os.path.join(dirpath, "application.fam")
        with open(path, errors="replace") as f:
            text = f.read()
        for match in pattern.finditer(text):
            for name in re.findall(r"""["']([^"']+)["']""", match.group(1)):
                found.setdefault(name, os.path.relpath(path, ROOT))
    return found


class LtoLists(unittest.TestCase):
    def test_fap_libs_are_not_lto(self):
        libs = fap_libs()
        self.assertTrue(
            libs, "found no fap_libs: is the test looking in the right place?"
        )
        missing = {n: p for n, p in libs.items() if n not in lto.NO_LTO_LIBS}
        self.assertFalse(
            missing,
            f"add to NO_LTO_LIBS in scripts/fbt/lto.py (first use in the file shown): {missing}",
        )

    def test_no_lto_libs_are_used(self):
        # A stale entry only costs flash, so this is a nudge, not a build break.
        libs = fap_libs()
        unused = [n for n in lto.NO_LTO_LIBS if n not in libs]
        self.assertFalse(unused, f"no app links these any more: {unused}")

    def test_wrappers_match_the_definitions(self):
        # a call such as __wrap_fgetc(stdin) is not a definition: only keep the
        # names that start a function definition (type and name at line start)
        defined = set()
        for rel in WRAPPER_SOURCES:
            with open(os.path.join(ROOT, rel)) as f:
                for line in f:
                    m = re.match(r"^[A-Za-z_][\w \*]*\b__wrap_(\w+)\s*\(", line)
                    if m:
                        defined.add(m.group(1))
        self.assertEqual(set(lto.WRAPPERS), defined)
        self.assertEqual(len(lto.WRAPPERS), len(set(lto.WRAPPERS)))

    def test_sources_exist(self):
        for rel in lto.NO_LTO_SOURCES:
            self.assertTrue(os.path.exists(os.path.join(ROOT, rel)), rel)


if __name__ == "__main__":
    unittest.main()
