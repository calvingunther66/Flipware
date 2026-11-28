#!/usr/bin/env python3
"""Read a GNU ld map file (-Map) and say where the flash went.

  linkmap.py firmware.elf.map [--top N] [--why SYMBOL ...]

Flash is counted per input section: everything the linker placed in flash, plus
the initial values of .data (placed in RAM, stored in flash). Each input section
is charged to the object file it came from, and objects are grouped by the
static library they sit in (libnfc.a) or, for loose object files, by their
first two directories (three under applications/). Only the Python standard
library is used.

`--why SYMBOL` reads the cross reference table (the link has to use
-Wl,--cref) and prints which file defines the symbol and which files use it.
"""

import argparse
import os
import re
import sys

LTO_GROUP = "(LTO partitions)"
FLASH_START = 0x08000000
FLASH_END = 0x08100000

# Output sections that are address space the linker reserves, not code or data:
# the free flash up to the radio stack and the heap/stack area in RAM. Their
# `*fill*` is not part of the image.
IGNORED_OUTPUT_SECTIONS = {".free_flash", "._user_heap_stack"}

_INPUT_ONE = re.compile(r"^ (\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S.*)$")
_INPUT_NAME = re.compile(r"^ (\S+)$")
_INPUT_REST = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S.*)$")
_NOLOAD = r"(?:\s+\(NOLOAD\))?"
_OUTPUT_ONE = re.compile(
    r"^(\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)"
    + _NOLOAD
    + r"(?:\s+load address 0x([0-9a-fA-F]+))?\s*$"
)
_OUTPUT_NAME = re.compile(r"^(\S+)$")
_OUTPUT_REST = re.compile(
    r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)"
    + _NOLOAD
    + r"(?:\s+load address 0x([0-9a-fA-F]+))?\s*$"
)
_FILL = re.compile(r"^ \*fill\*\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)")


def _in_flash(addr):
    return FLASH_START <= addr < FLASH_END


def parse_map(lines):
    """Return a list of input sections that take flash.

    Each is a dict: `section`, `output` (output section), `size`, `file` (as the
    map prints it) and `kind` ('code and data' or 'data init' or 'padding').
    Output sections marked NOLOAD (the free flash filler, RAM buffers) are left
    out.
    """
    out = []
    in_map = False
    output = {"name": None, "lma": None, "noload": False}
    pending_input = None
    pending_output = None

    def set_output(name, lma, noload):
        output.update(name=name, lma=int(lma, 16) if lma else None, noload=noload)

    def add(section, addr, size, path):
        if not size or output["noload"] or output["name"] in IGNORED_OUTPUT_SECTIONS:
            return
        if _in_flash(addr):
            kind = "code and data"
        elif output["lma"] is not None and _in_flash(output["lma"]):
            kind = "data init"
        else:
            return
        out.append(
            {
                "section": section,
                "output": output["name"],
                "size": size,
                "file": path,
                "kind": kind,
            }
        )

    for raw in lines:
        line = raw.rstrip("\n")
        if not in_map:
            if line.startswith("Linker script and memory map"):
                in_map = True
            continue
        if line.startswith("Cross Reference Table"):
            break

        if pending_output is not None:
            m = _OUTPUT_REST.match(line)
            name, pending_output = pending_output, None
            if m:
                set_output(name, m.group(3), "(NOLOAD)" in line)
                continue
            # Not a header we understand: ignore what follows until the next one.
            set_output(name, None, True)
        if pending_input is not None:
            m = _INPUT_REST.match(line)
            name, pending_input = pending_input, None
            if m:
                add(name, int(m.group(1), 16), int(m.group(2), 16), m.group(3).strip())
                continue

        if line.startswith(" "):
            m = _INPUT_ONE.match(line)
            if m:
                if m.group(1) != "*fill*":
                    add(
                        m.group(1),
                        int(m.group(2), 16),
                        int(m.group(3), 16),
                        m.group(4).strip(),
                    )
                continue
            m = _FILL.match(line)
            if m:
                add("*fill*", int(m.group(1), 16), int(m.group(2), 16), "(padding)")
                continue
            # A long section name sits alone on its line and the numbers follow
            # on the next one. Custom sections (COMMON, MB_MEM2) have no dot.
            m = _INPUT_NAME.match(line)
            if m:
                pending_input = m.group(1)
            continue

        m = _OUTPUT_ONE.match(line)
        if m:
            set_output(m.group(1), m.group(4), "(NOLOAD)" in line)
            continue
        m = _OUTPUT_NAME.match(line)
        if m:
            pending_output = m.group(1)
    return out


_ARCHIVE = re.compile(r"^(?P<archive>.+\.a)\((?P<member>[^()]+)\)$")
# The partitions a link-time optimized build compiles at link time, named after a
# temporary file (ccAbC123.ltrans4.ltrans.o): the name changes on every build.
_LTO_PARTITION = re.compile(r"\.ltrans\d+\.ltrans\.o$")
_BUILD_PREFIX = re.compile(r"^(?:.*/)?build/[^/]+/")


def classify(path):
    """Return (group, object) for a path from the map.

    The group is the static library for archive members, `(padding)` for fill,
    `(LTO partitions)` for the code a link-time optimized build compiles at link
    time (it no longer belongs to one library), or the first two directories of
    the object file under the build directory (three under applications/).
    """
    if path == "(padding)":
        return "(padding)", "(padding)"
    if _LTO_PARTITION.search(path):
        # The code is spread over partitions and no longer belongs to one library.
        return LTO_GROUP, LTO_GROUP
    m = _ARCHIVE.match(path)
    if m:
        archive = os.path.basename(m.group("archive"))
        return archive, m.group("member")
    short = _BUILD_PREFIX.sub("", path, count=1)
    if short.startswith("/") or ":" in short[:3]:
        # An object outside the build directory (the toolchain's crt files).
        return "(toolchain)/" + os.path.basename(path), os.path.basename(path)
    parts = short.split("/")
    depth = 3 if parts[0] == "applications" else 2
    if len(parts) > depth:
        group = "/".join(parts[:depth])
    else:
        group = "/".join(parts[:-1]) or "(root)"
    return group, short


def summarize(sections):
    """Totals per group and per object, in bytes of flash."""
    groups, objects = {}, {}
    for s in sections:
        group, obj = classify(s["file"])
        groups[group] = groups.get(group, 0) + s["size"]
        # A loose object's path already starts with its group; say it once.
        key = obj if obj.startswith(group + "/") else f"{group}:{obj}"
        objects[key] = objects.get(key, 0) + s["size"]
    return groups, objects


def parse_cref(lines):
    """Cross reference table: symbol -> (defining file, [files that use it])."""
    out = {}
    in_table = False
    current = None
    for raw in lines:
        line = raw.rstrip("\n")
        if not in_table:
            if line.startswith("Cross Reference Table"):
                in_table = True
            continue
        if not line.strip() or line.startswith("Symbol"):
            continue
        if not line.startswith(" "):
            parts = line.split(None, 1)
            if len(parts) == 2:
                current = parts[0]
                out[current] = (parts[1].strip(), [])
            else:
                current = parts[0]
                out[current] = (None, [])
        elif current is not None:
            first = out[current][0]
            if first is None:
                out[current] = (line.strip(), [])
            else:
                out[current][1].append(line.strip())
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("map")
    parser.add_argument("--top", type=int, default=30)
    parser.add_argument("--why", action="append", default=[])
    args = parser.parse_args(argv)
    with open(args.map, errors="replace") as f:
        lines = f.readlines()
    sections = parse_map(lines)
    groups, objects = summarize(sections)
    total = sum(groups.values())
    print(f"Flash in the map: {total:,} B (input sections, before string merging)")
    by_output = {}
    for sec in sections:
        by_output[sec["output"]] = by_output.get(sec["output"], 0) + sec["size"]
    for name, size in sorted(by_output.items(), key=lambda kv: -kv[1]):
        print(f"{size:>10,}  output section {name}")
    print()
    for name, size in sorted(groups.items(), key=lambda kv: -kv[1])[: args.top]:
        print(f"{size:>10,}  {name}")
    print()
    for name, size in sorted(objects.items(), key=lambda kv: -kv[1])[: args.top]:
        print(f"{size:>10,}  {name}")
    cref = parse_cref(lines) if args.why else {}
    for sym in args.why:
        if sym in cref:
            defined, users = cref[sym]
            print(f"\n{sym} is defined in {defined}, used by:")
            for u in users:
                print(f"  {u}")
        else:
            print(f"\n{sym}: not in the cross reference table")
    return 0


if __name__ == "__main__":
    sys.exit(main())
