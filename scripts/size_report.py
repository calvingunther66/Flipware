#!/usr/bin/env python3
"""Flash and RAM report for a Flipware firmware build, with gates for CI.

Reads firmware.elf and the update package's update.fuf (for the radio stack
address) and reports how much flash and RAM the firmware uses, how much flash is
free below the radio stack, and how that compares with an earlier build.

  size_report.py --elf firmware.elf --update-dir dist/f7-C/f7-update-X \\
      --budget .github/size_budget.json [--baseline size-f7.json] \\
      [--map firmware.elf.map] [--growth-allowed] [--target f7] \\
      --out-json size-f7.json --out-md size.md

With --map the report also says which library or directory each byte of flash
came from (see scripts/linkmap.py).

Exit status is 1 when a gate in the budget file fails, after the report is
written. Only the Python standard library is used, so it runs anywhere.
"""

import argparse
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import linkmap  # noqa: E402  (same folder; reads the linker map)

FLASH_START = 0x08000000
FLASH_END = 0x08100000
RAM_START = 0x20000000
RAM_END = 0x20040000
PAGE = 4096

# Marker at the top of the markdown report, so a CI step can find and update the
# comment it posted earlier instead of adding a new one.
MARKER = "<!-- flipware-size-report -->"

# Linker bookkeeping sections that don't hold anything.
IGNORED_SECTIONS = {".free_flash", "._user_heap_stack"}

SHF_ALLOC = 2
SHT_NOBITS = 8
PT_LOAD = 1
STT_OBJECT = 1
STT_FUNC = 2


class ElfError(Exception):
    pass


def _unpack(fmt, data, offset):
    return struct.unpack_from("<" + fmt, data, offset)


def read_elf(path):
    """Parse the parts of a little-endian ELF32/ELF64 file this report needs.

    Returns a dict with `sections` (name, type, flags, addr, size), `segments`
    (type, paddr, filesz), `symbols` (name -> total size of functions and data
    objects with that name) and `symbol_values` (name -> value, every symbol).
    """
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF":
        raise ElfError(f"{path} is not an ELF file")
    if data[5] != 1:
        raise ElfError("only little-endian ELF files are supported")
    is64 = {1: False, 2: True}.get(data[4])
    if is64 is None:
        raise ElfError("unknown ELF class")

    if is64:
        phoff, shoff = _unpack("QQ", data, 32)
        phentsize, phnum, shentsize, shnum, shstrndx = _unpack("HHHHH", data, 54)
    else:
        phoff, shoff = _unpack("II", data, 28)
        phentsize, phnum, shentsize, shnum, shstrndx = _unpack("HHHHH", data, 42)

    def cstr(table_offset, name_offset):
        start = table_offset + name_offset
        return data[start : data.index(b"\0", start)].decode("utf-8", "replace")

    raw_sections = []
    for i in range(shnum):
        base = shoff + i * shentsize
        if is64:
            name, typ, flags, addr, offset, size, link, info, _, entsize = _unpack(
                "IIQQQQIIQQ", data, base
            )
        else:
            name, typ, flags, addr, offset, size, link, info, _, entsize = _unpack(
                "IIIIIIIIII", data, base
            )
        raw_sections.append((name, typ, flags, addr, offset, size, link, entsize))

    strtab_offset = raw_sections[shstrndx][4] if raw_sections else 0
    sections = []
    for name, typ, flags, addr, offset, size, link, entsize in raw_sections:
        sections.append(
            {
                "name": cstr(strtab_offset, name) if typ != 0 else "",
                "type": typ,
                "flags": flags,
                "addr": addr,
                "size": size,
            }
        )

    segments = []
    for i in range(phnum):
        base = phoff + i * phentsize
        if is64:
            typ, _, _, _, paddr, filesz, _, _ = _unpack("IIQQQQQQ", data, base)
        else:
            typ, _, _, paddr, filesz, _, _, _ = _unpack("IIIIIIII", data, base)
        segments.append({"type": typ, "paddr": paddr, "filesz": filesz})

    symbols = {}
    symbol_values = {}
    for index, section in enumerate(raw_sections):
        name, typ, flags, addr, offset, size, link, entsize = section
        if typ != 2:  # SHT_SYMTAB
            continue
        names_offset = raw_sections[link][4]
        entry = 24 if is64 else 16
        for pos in range(offset, offset + size, entry):
            if is64:
                sym_name, info, _, _, value, sym_size = _unpack("IBBHQQ", data, pos)
            else:
                sym_name, value, sym_size, info, _, _ = _unpack("IIIBBH", data, pos)
            if not sym_name:
                continue
            sym = cstr(names_offset, sym_name)
            symbol_values.setdefault(sym, value)
            if sym_size and (info & 0xF) in (STT_OBJECT, STT_FUNC):
                symbols[sym] = symbols.get(sym, 0) + sym_size
    return {
        "sections": sections,
        "segments": segments,
        "symbols": symbols,
        "symbol_values": symbol_values,
    }


def parse_radio_address(manifest_text):
    """Return the `Radio address` from the text of an update.fuf manifest.

    The manifest is a Flipper File Format file, and scripts/update.py writes
    integers in it as hex bytes, least significant byte first: 0x080D7000 is
    `Radio address: 00 70 0D 08`.
    """
    match = re.search(
        r"^Radio address:[ \t]*((?:[0-9A-Fa-f]{2}[ \t]*)+)$", manifest_text, re.M
    )
    if not match:
        raise ValueError("update.fuf has no 'Radio address' line")
    value = int.from_bytes(bytes.fromhex("".join(match.group(1).split())), "little")
    if value == 0:
        raise ValueError(
            "update.fuf has radio address 0 (a package without a radio stack)"
        )
    return value


def build_report(elf, radio_address, target="f7", dfu_size=None):
    """Turn a parsed ELF and the radio address into the numbers we track."""
    flash_end = FLASH_START
    for seg in elf["segments"]:
        if (
            seg["type"] == PT_LOAD
            and seg["filesz"]
            and FLASH_START <= seg["paddr"] < FLASH_END
        ):
            flash_end = max(flash_end, seg["paddr"] + seg["filesz"])
    image_size = flash_end - FLASH_START

    sections = []
    for sec in elf["sections"]:
        if not sec["name"] or sec["name"] in IGNORED_SECTIONS:
            continue
        if not sec["flags"] & SHF_ALLOC or not sec["size"]:
            continue
        if FLASH_START <= sec["addr"] < FLASH_END:
            region = "flash"
        elif RAM_START <= sec["addr"] < RAM_END:
            region = "ram"
        else:
            continue
        sections.append({"name": sec["name"], "region": region, "size": sec["size"]})

    def size_of(name):
        return sum(s["size"] for s in sections if s["name"] == name)

    free_bytes = radio_address - flash_end
    # Pages that are completely free: from the first page boundary after the
    # image up to the radio stack.
    first_free_page = -(-flash_end // PAGE) * PAGE
    free_pages = max(0, (radio_address - first_free_page) // PAGE)

    values = elf["symbol_values"]
    heap = None
    if "__heap_start__" in values and "__heap_end__" in values:
        heap = values["__heap_end__"] - values["__heap_start__"]

    report = {
        "target": target,
        "image_size": image_size,
        "radio_address": radio_address,
        "free_flash": free_bytes,
        "free_pages": free_pages,
        "data": size_of(".data"),
        "bss": size_of(".bss"),
        "heap": heap,
        "sections": sections,
        "symbols": elf["symbols"],
    }
    if dfu_size is not None:
        report["dfu_size"] = dfu_size
    return report


MAP_TOP_OBJECTS = 40


def add_map(report, map_lines):
    """Add flash attribution from a linker map: bytes per library or directory,
    and the biggest object files."""
    groups, objects = linkmap.summarize(linkmap.parse_map(map_lines))
    top = sorted(objects.items(), key=lambda kv: (-kv[1], kv[0]))[:MAP_TOP_OBJECTS]
    report["flash_groups"] = groups
    report["flash_objects"] = dict(top)
    return report


def compare(current, baseline):
    """Differences between two reports (current minus baseline)."""
    delta = {
        "image_size": current["image_size"] - baseline["image_size"],
        "free_flash": current["free_flash"] - baseline["free_flash"],
        "data": current["data"] - baseline["data"],
        "bss": current["bss"] - baseline["bss"],
    }
    if current.get("heap") is not None and baseline.get("heap") is not None:
        delta["heap"] = current["heap"] - baseline["heap"]

    base_sections = {s["name"]: s["size"] for s in baseline["sections"]}
    delta["sections"] = {
        s["name"]: s["size"] - base_sections.get(s["name"], 0)
        for s in current["sections"]
    }

    before, after = baseline["symbols"], current["symbols"]
    changes = []
    for name in set(before) | set(after):
        change = after.get(name, 0) - before.get(name, 0)
        if change:
            changes.append((name, before.get(name, 0), after.get(name, 0), change))
    delta["symbols_grown"] = sorted(changes, key=lambda c: (-c[3], c[0]))
    delta["symbols_shrunk"] = sorted(changes, key=lambda c: (c[3], c[0]))

    if current.get("flash_groups") is not None:
        was = baseline.get("flash_groups") or {}
        delta["flash_groups"] = {
            name: size - was.get(name, 0)
            for name, size in current["flash_groups"].items()
        }
        for name in was:
            delta["flash_groups"].setdefault(name, -was[name])
        delta["has_group_baseline"] = bool(was)
    return delta


def check_gates(current, delta, budget, growth_allowed):
    """Return a list of messages, one per gate that fails."""
    problems = []
    minimum = budget.get("min_free_flash_bytes")
    if minimum is not None and current["free_flash"] < minimum:
        problems.append(
            f"Free flash is {current['free_flash']} B, below the minimum of {minimum} B "
            f"(`min_free_flash_bytes` in the size budget). Make the firmware smaller, or "
            f"move code to an app on the SD card. The `{budget.get('label', 'flash-budget')}` "
            f"label does not lift this minimum."
        )
    max_growth = budget.get("max_growth_bytes")
    if delta is not None and max_growth is not None and not growth_allowed:
        grown = delta["image_size"]
        if grown > max_growth:
            label = budget.get("label", "flash-budget")
            problems.append(
                f"The firmware image grew by {grown} B, more than the {max_growth} B a PR "
                f"may add. Shrink it, or have the maintainer add the `{label}` label and "
                f"re-run this job."
            )
    return problems


def _code(name):
    """A symbol name safe to put in a markdown table cell inside backticks."""
    clean = re.sub(r"[^A-Za-z0-9_.$@<>:+*&~!,()\[\]/ -]", "?", name)
    if len(clean) > 80:
        clean = clean[:77] + "..."
    return f"`{clean}`"


def _kib(n):
    return f"{n / 1024:.2f} KiB"


def _signed(n):
    return f"{n:+,}"


MAP_TOP_GROUPS = 40


def render_markdown(current, delta, problems, baseline_note, budget):
    t = current["target"]
    out = [MARKER, f"### Flash and RAM report ({t})", ""]
    if problems:
        out.append("**Failed:**")
        out.extend(f"- {p}" for p in problems)
        out.append("")
    out.append("| | Now | Change |")
    out.append("|---|---:|---:|")

    def row(label, now, key, fmt=lambda v: f"{v:,} B"):
        change = _signed(delta[key]) + " B" if delta and key in delta else "n/a"
        out.append(f"| {label} | {fmt(now)} | {change} |")

    row("Image size", current["image_size"], "image_size")
    row(
        "Free flash below radio stack",
        current["free_flash"],
        "free_flash",
        lambda v: f"{v:,} B ({_kib(v)}), {current['free_pages']} free 4 KiB page(s)",
    )
    row(".data", current["data"], "data")
    row(".bss", current["bss"], "bss")
    if current.get("heap") is not None:
        row("Heap (RAM left after .data, .bss and the stack)", current["heap"], "heap")
    out.append("")
    minimum = budget.get("min_free_flash_bytes")
    if minimum is not None:
        out.append(
            f"Gate: free flash must stay at or above {minimum:,} B "
            f"(the `{budget.get('label', 'flash-budget')}` label does not lift this)."
        )
    if budget.get("max_growth_bytes") is not None:
        out.append(
            f"Gate: a PR may grow the image by at most {budget['max_growth_bytes']} B "
            f"unless it has the `{budget.get('label', 'flash-budget')}` label."
        )
    out.append(f"Compared with: {baseline_note}.")
    out.append("")

    out.append("<details><summary>Sections</summary>\n")
    out.append("| Section | Region | Size | Change |")
    out.append("|---|---|---:|---:|")
    for sec in sorted(current["sections"], key=lambda s: -s["size"]):
        change = _signed(delta["sections"].get(sec["name"], 0)) if delta else "n/a"
        out.append(
            f"| {_code(sec['name'])} | {sec['region']} | {sec['size']:,} | {change} |"
        )
    out.append("\n</details>\n")

    if current.get("flash_groups"):
        groups = current["flash_groups"]
        total = sum(groups.values())
        changes = (
            (delta or {}).get("flash_groups")
            if (delta or {}).get("has_group_baseline")
            else None
        )
        out.append(
            f"<details open><summary>Flash by library or directory (from the linker map, {total:,} B)</summary>\n"
        )
        out.append("| Where | Flash | Share | Change |")
        out.append("|---|---:|---:|---:|")
        for name, size in sorted(groups.items(), key=lambda kv: (-kv[1], kv[0]))[
            :MAP_TOP_GROUPS
        ]:
            change = _signed(changes.get(name, 0)) if changes is not None else "n/a"
            out.append(
                f"| {_code(name)} | {size:,} | {100 * size / total:.1f}% | {change} |"
            )
        out.append("\n</details>\n")
        out.append("<details><summary>Biggest object files</summary>\n")
        out.append("| Object | Flash |")
        out.append("|---|---:|")
        for name, size in list(current["flash_objects"].items())[:20]:
            out.append(f"| {_code(name)} | {size:,} |")
        out.append("\n</details>\n")

    if delta:
        grown = [c for c in delta["symbols_grown"] if c[3] > 0][:10]
        shrunk = [c for c in delta["symbols_shrunk"] if c[3] < 0][:5]
        for title, rows in (
            ("Symbols that grew most", grown),
            ("Symbols that shrank most", shrunk),
        ):
            out.append(f"<details><summary>{title}</summary>\n")
            if rows:
                out.append("| Symbol | Before | After | Change |")
                out.append("|---|---:|---:|---:|")
                for name, was, now, change in rows:
                    out.append(
                        f"| {_code(name)} | {was:,} | {now:,} | {_signed(change)} |"
                    )
            else:
                out.append("None.")
            out.append("\n</details>\n")
    return "\n".join(out) + "\n"


def load_json(path):
    with open(path) as f:
        return json.load(f)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--elf", required=True)
    parser.add_argument("--update-dir", required=True)
    parser.add_argument("--budget", required=True)
    parser.add_argument("--baseline", help="size JSON of an earlier build")
    parser.add_argument(
        "--baseline-note",
        default="the earlier build",
        help="what the baseline is, for the report",
    )
    parser.add_argument(
        "--map", help="linker map (firmware.elf.map) to attribute flash"
    )
    parser.add_argument("--growth-allowed", action="store_true")
    parser.add_argument("--target", default="f7")
    parser.add_argument("--out-json", required=True)
    parser.add_argument("--out-md", required=True)
    args = parser.parse_args(argv)

    with open(os.path.join(args.update_dir, "update.fuf")) as f:
        radio_address = parse_radio_address(f.read())
    dfu_path = os.path.join(args.update_dir, "firmware.dfu")
    dfu_size = os.path.getsize(dfu_path) if os.path.exists(dfu_path) else None

    current = build_report(read_elf(args.elf), radio_address, args.target, dfu_size)
    if args.map:
        with open(args.map, errors="replace") as f:
            add_map(current, f.readlines())
    budget = load_json(args.budget)

    delta = None
    note = "nothing (no earlier build to compare with)"
    if args.baseline and os.path.exists(args.baseline):
        baseline = load_json(args.baseline)
        if baseline.get("target") == current["target"]:
            delta = compare(current, baseline)
            note = args.baseline_note

    problems = check_gates(current, delta, budget, args.growth_allowed)
    with open(args.out_json, "w") as f:
        json.dump(current, f, separators=(",", ":"), sort_keys=True)
    markdown = render_markdown(current, delta, problems, note, budget)
    with open(args.out_md, "w") as f:
        f.write(markdown)
    print(markdown)
    for p in problems:
        print(f"::error title=Flash budget::{p}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
