#!/usr/bin/env python3
"""Helpers for .github/workflows/release.yml.

  release.py notes --tag TAG --changelog CHANGELOG.md --template release.md --out notes.md
  release.py sums  --out SHA256SUMS FILE [FILE ...]
  release.py check-tag TAG        (exit 1 if TAG is not a release tag)
  release.py is-prerelease TAG    (prints "true" or "false")

Release tags look like `flpw-001`; release candidates add `-rcN`, for example
`flpw-001-rc1`, and are published as pre-releases.
"""

import argparse
import hashlib
import os
import re
import sys

TAG_RE = re.compile(r"flpw-(?P<number>[0-9]+)(?P<rc>-rc[0-9]+)?")


def parse_tag(tag):
    """Return (base_tag, is_rc) for a release tag, or None if it isn't one."""
    match = TAG_RE.fullmatch(tag)
    if match is None:
        return None
    return "flpw-" + match.group("number"), match.group("rc") is not None


def is_prerelease(tag):
    parsed = parse_tag(tag)
    return parsed is not None and parsed[1]


def extract_section(changelog, tag):
    """Return the body of the CHANGELOG.md section for `tag`.

    A section starts at a level-2 heading whose first word is the tag, for
    example `## flpw-001 - 2027-01-15`, and runs to the next level-2 heading.
    Returns None when there is no such section or it is empty.
    """
    heading = re.compile(r"^##[ \t]+\[?" + re.escape(tag) + r"\]?(?=[ \t]|$)", re.M)
    start = heading.search(changelog)
    if start is None:
        return None
    body_start = changelog.find("\n", start.end())
    if body_start == -1:
        return None
    next_heading = re.compile(r"^##[ \t]", re.M).search(changelog, body_start)
    body = changelog[body_start : next_heading.start() if next_heading else None]
    body = body.strip()
    return body or None


def release_notes_body(changelog, tag):
    """Pick the changelog text for `tag`, or return None.

    A final release (`flpw-001`) needs its own section: the maintainer renames
    `## Unreleased` before tagging. A release candidate is cut earlier, so it
    uses its own section if there is one, then the final tag's section, then
    `## Unreleased`.
    """
    parsed = parse_tag(tag)
    candidates = [tag]
    if parsed is not None and parsed[1]:
        candidates += [parsed[0], "Unreleased"]
    for name in candidates:
        body = extract_section(changelog, name)
        if body is not None:
            return body
    return None


def render_notes(template, tag, changelog_body):
    prerelease_note = ""
    if is_prerelease(tag):
        prerelease_note = (
            "> **Release candidate.** This is a test build for the next release. "
            "Expect rough edges, and back up your SD card first.\n\n"
        )
    return template.format(
        VERSION_TAG=tag, CHANGELOG=changelog_body, PRERELEASE_NOTE=prerelease_note
    )


def sha256sums(paths):
    """Return the text of a SHA256SUMS file (`sha256sum` format, bare file names)."""
    lines = []
    for path in sorted(paths, key=os.path.basename):
        digest = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                digest.update(chunk)
        lines.append(f"{digest.hexdigest()}  {os.path.basename(path)}\n")
    return "".join(lines)


def cmd_notes(args):
    if parse_tag(args.tag) is None:
        sys.exit(
            f"'{args.tag}' is not a release tag (expected flpw-001 or flpw-001-rc1)."
        )
    with open(args.changelog, "r") as f:
        body = release_notes_body(f.read(), args.tag)
    if body is None:
        sys.exit(
            f"CHANGELOG.md has no section for {args.tag}. "
            f"Rename '## Unreleased' to '## {args.tag} - YYYY-MM-DD', merge it, and "
            "re-run the release job."
        )
    with open(args.template, "r") as f:
        template = f.read()
    with open(args.out, "w") as f:
        f.write(render_notes(template, args.tag, body))


def cmd_sums(args):
    with open(args.out, "w") as f:
        f.write(sha256sums(args.files))


def cmd_check_tag(args):
    if parse_tag(args.tag) is None:
        sys.exit(
            f"'{args.tag}' is not a release tag (expected flpw-001 or flpw-001-rc1)."
        )


def cmd_is_prerelease(args):
    print("true" if is_prerelease(args.tag) else "false")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("notes")
    p.add_argument("--tag", required=True)
    p.add_argument("--changelog", required=True)
    p.add_argument("--template", required=True)
    p.add_argument("--out", required=True)
    p.set_defaults(func=cmd_notes)

    p = sub.add_parser("sums")
    p.add_argument("--out", required=True)
    p.add_argument("files", nargs="+")
    p.set_defaults(func=cmd_sums)

    p = sub.add_parser("check-tag")
    p.add_argument("tag")
    p.set_defaults(func=cmd_check_tag)

    p = sub.add_parser("is-prerelease")
    p.add_argument("tag")
    p.set_defaults(func=cmd_is_prerelease)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
