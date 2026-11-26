#!/usr/bin/env python
import json
import os
import re
import sys


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


if __name__ == "__main__":
    with open(os.environ["GITHUB_EVENT_PATH"], "r") as f:
        event = json.load(f)
    import requests

    release = requests.get(
        event["release"]["url"],
        headers={
            "Accept": "application/vnd.github.v3+json",
            "Authorization": f"token {os.environ['GITHUB_TOKEN']}"
        }
    ).json()
    version_tag = release["tag_name"]

    with open("CHANGELOG.md", "r") as f:
        changelog = extract_section(f.read(), version_tag)
    if changelog is None:
        sys.exit(
            f"CHANGELOG.md has no section for {version_tag}. "
            f"Rename '## Unreleased' to '## {version_tag} - YYYY-MM-DD' and re-run."
        )

    notes_path = '.github/workflow_data/release.md'
    with open(notes_path, "r") as f:
        template = f.read()
    notes = template.format(
        VERSION_TAG=version_tag,
        CHANGELOG=changelog
    )
    with open(notes_path, "w") as f:
        f.write(notes)
