"""Checks that the table in helpers/subghz_plugins_table.c, the plugin sources under
applications/main/subghz/plugins and the App() entries in application.fam agree, so a protocol
cannot be moved into a plugin without the app knowing which file to load for it.

Run it with tests/host/subghz_plugins/run.sh; it needs nothing but Python 3.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
APP = ROOT / "applications/main/subghz"
PLUGINS = APP / "plugins"
TABLE_C = APP / "helpers/subghz_plugins_table.c"
FAM = APP / "application.fam"
ITEMS_C = ROOT / "lib/subghz/protocols/protocol_items.c"

errors = []


def fail(message):
    errors.append(message)


def parse_table():
    text = TABLE_C.read_text()
    names = {}
    for match in re.finditer(r"static const char\* const (\w+)_names\[\] = \{(.*?)\};", text, re.S):
        names[match.group(1)] = re.findall(r'"([^"]*)"', match.group(2))
    rows = []
    for match in re.finditer(
        r'\{\s*"(\w+)",\s*"([\w.]+)",\s*SubGhzProtocolFlag_(AM|FM),\s*(\w+)_names,', text
    ):
        rows.append(
            {
                "id": match.group(1),
                "file": match.group(2),
                "modulation": match.group(3),
                "names": names.get(match.group(4)),
                "names_var": match.group(4),
            }
        )
    return rows


def parse_fam():
    text = FAM.read_text()
    apps = {}
    for block in re.findall(r"App\((.*?)\n\)", text, re.S):
        appid = re.search(r'appid="([^"]+)"', block)
        if not appid or not appid.group(1).startswith("subghz_plugin_"):
            continue
        apps[appid.group(1)] = {
            "entry": re.search(r'entry_point="([^"]+)"', block).group(1),
            "sources": re.findall(r'"(plugins/[^"]+)"', block),
            "embedded": "fal_embedded=True" in block,
            "type_plugin": "FlipperAppType.PLUGIN" in block,
            "requires": re.search(r'requires=\["([^"]+)"\]', block).group(1),
        }
    return apps


def main():
    rows = parse_table()
    apps = parse_fam()
    if not rows:
        fail("no rows found in the bundle table")

    ids = [row["id"] for row in rows]
    if len(set(ids)) != len(ids):
        fail("duplicate bundle ids in the table")

    protocol_items = ITEMS_C.read_text()
    seen_names = {}
    for row in rows:
        bundle = row["id"]
        if row["names"] is None:
            fail(f"{bundle}: names array {row['names_var']}_names not found")
            continue

        # The app manifest has a plugin app for the row, with the file name the table says
        appid = f"subghz_plugin_{bundle}"
        app = apps.get(appid)
        if not app:
            fail(f"{bundle}: no App(appid=\"{appid}\") in application.fam")
            continue
        if row["file"] != f"{appid}.fal":
            fail(f"{bundle}: table file {row['file']} is not {appid}.fal")
        if not (app["embedded"] and app["type_plugin"] and app["requires"] == "subghz"):
            fail(f"{bundle}: {appid} must be an embedded PLUGIN that requires subghz")
        if f"plugins/{bundle}/*.c" not in app["sources"]:
            fail(f"{bundle}: {appid} does not build plugins/{bundle}/*.c")

        # Entry point is defined by the bundle source
        plugin_dir = PLUGINS / bundle
        if not plugin_dir.is_dir():
            fail(f"{bundle}: directory plugins/{bundle} is missing")
            continue
        defined = re.findall(
            r"SUBGHZ_PROTOCOL_PLUGIN_DEFINE\(\s*(\w+)", "".join(p.read_text() for p in plugin_dir.glob("*.c"))
        )
        if defined != [app["entry"]]:
            fail(f"{bundle}: entry point {app['entry']} vs defined {defined}")

        # Protocol names in the table are exactly the *_NAME strings of the bundle's headers,
        # minus the helpers' own (the generic headers define none)
        header_names = []
        for header in sorted(plugin_dir.glob("*.h")):
            header_names += re.findall(r'#define\s+\w+_NAME\s+"([^"]+)"', header.read_text())
        if sorted(header_names) != sorted(row["names"]):
            fail(
                f"{bundle}: table has {sorted(row['names'])} but the headers define {sorted(header_names)}"
            )

        # Each protocol is listed in the bundle's entry point
        plugin_source = "".join(p.read_text() for p in plugin_dir.glob("*_plugin.c"))
        listed = re.findall(r"&(\w+_protocol_\w+|subghz_protocol_\w+)", plugin_source)
        defined_protocols = []
        for source in sorted(plugin_dir.glob("*.c")):
            defined_protocols += re.findall(
                r"^const SubGhzProtocol (\w+) = \{", source.read_text(), re.M
            )
        if sorted(listed) != sorted(defined_protocols):
            fail(f"{bundle}: entry point lists {sorted(listed)}, sources define {sorted(defined_protocols)}")

        for name in row["names"]:
            if name in seen_names:
                fail(f"{name!r} is in both {seen_names[name]} and {bundle}")
            seen_names[name] = bundle

    # No plugin protocol is also built into the firmware registry
    for bundle_dir in sorted(p for p in PLUGINS.iterdir() if p.is_dir() and p.name != "ws_common"):
        for source in bundle_dir.glob("*.c"):
            for symbol in re.findall(r"^const SubGhzProtocol (\w+) = \{", source.read_text(), re.M):
                if re.search(rf"^\s*&{symbol},", protocol_items, re.M):
                    fail(f"{symbol} is in plugins/{bundle_dir.name} and in protocol_items.c")

    # Every directory with a plugin source has a table row
    for bundle_dir in sorted(p for p in PLUGINS.iterdir() if p.is_dir() and p.name != "ws_common"):
        if bundle_dir.name not in ids:
            fail(f"plugins/{bundle_dir.name} has no row in the table")

    # Feed order the decoders rely on: Bresser 3CH before LaCrosse, Solight TE44 before Auriol
    flat = []
    for row in rows:
        flat += row["names"] or []
    for first, second in (("Bresser-3CH", "LaCrosse_TX"), ("Solight TE44", "Auriol HG06061")):
        if first not in flat or second not in flat or flat.index(first) > flat.index(second):
            fail(f"{first} has to come before {second} in the table")

    if errors:
        for message in errors:
            print("FAIL:", message)
        return 1
    print(f"ok: {len(rows)} bundles, {len(seen_names)} protocols")
    return 0


if __name__ == "__main__":
    sys.exit(main())
