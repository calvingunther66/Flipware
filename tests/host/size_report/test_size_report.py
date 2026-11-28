"""Host test for scripts/size_report.py: reads a small ELF built here, checks the
numbers, the comparison with an earlier build and the CI gates.
  python3 -I tests/host/size_report/test_size_report.py   (or run.sh)
"""
import importlib.util
import json
import os
import struct
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))

spec = importlib.util.spec_from_file_location(
    "size_report", os.path.join(ROOT, "scripts", "size_report.py")
)
size_report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(size_report)

FLASH = 0x08000000
RAM = 0x20000008
RADIO = 0x080D7000


def make_elf(text, rodata, data, bss, symbols, heap=(0x20001000, 0x2002FC00), is64=False):
    """Build a tiny ARM-like ELF: .text and .rodata in flash, .data with its load
    copy in flash, .bss, the linker's .free_flash, and a symbol table.

    `symbols` is a list of (name, size, kind) with kind 'func' or 'object'.
    """
    flash_addr = FLASH
    text_addr = flash_addr
    rodata_addr = text_addr + text
    data_lma = rodata_addr + rodata
    data_addr = RAM
    bss_addr = data_addr + data

    strtab = b"\0"
    sym_entries = []
    all_symbols = list(symbols) + [
        ("__heap_start__", 0, "notype", heap[0]),
        ("__heap_end__", 0, "notype", heap[1]),
    ]
    for entry in all_symbols:
        name, size, kind = entry[:3]
        value = entry[3] if len(entry) > 3 else text_addr
        offset = len(strtab)
        strtab += name.encode() + b"\0"
        typ = {"func": 2, "object": 1, "notype": 0}[kind]
        sym_entries.append((offset, value, size, typ))
    if is64:
        symtab = b"\0" * 24 + b"".join(
            struct.pack("<IBBHQQ", n, t, 0, 1, v, s) for n, v, s, t in sym_entries
        )
    else:
        symtab = b"\0" * 16 + b"".join(
            struct.pack("<IIIBBH", n, v, s, t, 0, 1) for n, v, s, t in sym_entries
        )

    shstrtab = b"\0"
    names = {}
    for name in (".text", ".rodata", ".data", ".bss", ".free_flash", ".symtab", ".strtab", ".shstrtab"):
        names[name] = len(shstrtab)
        shstrtab += name.encode() + b"\0"

    body = b""
    ehsize = 64 if is64 else 52
    phentsize = 56 if is64 else 32
    shentsize = 64 if is64 else 40
    phnum = 2
    offset = ehsize + phentsize * phnum

    def put(blob):
        nonlocal body, offset
        start = offset
        body += blob
        offset += len(blob)
        return start

    text_off = put(b"\x01" * text)
    rodata_off = put(b"\x02" * rodata)
    data_off = put(b"\x03" * data)
    symtab_off = put(symtab)
    strtab_off = put(strtab)
    shstrtab_off = put(shstrtab)
    shoff = offset

    # (name, type, flags, addr, offset, size, link, info, entsize)
    sections = [
        (0, 0, 0, 0, 0, 0, 0, 0, 0),
        (names[".text"], 1, 6, text_addr, text_off, text, 0, 0, 0),
        (names[".rodata"], 1, 2, rodata_addr, rodata_off, rodata, 0, 0, 0),
        (names[".data"], 1, 3, data_addr, data_off, data, 0, 0, 0),
        (names[".bss"], 8, 3, bss_addr, 0, bss, 0, 0, 0),
        # The linker's marker for "the rest of the 1 MiB flash": must be ignored.
        (names[".free_flash"], 1, 0, data_lma, 0, 0x100000 - (data_lma - FLASH), 0, 0, 0),
        (names[".symtab"], 2, 0, 0, symtab_off, len(symtab), 7, 1, 24 if is64 else 16),
        (names[".strtab"], 3, 0, 0, strtab_off, len(strtab), 0, 0, 0),
        (names[".shstrtab"], 3, 0, 0, shstrtab_off, len(shstrtab), 0, 0, 0),
    ]
    sh = b""
    for n, t, fl, ad, of, sz, lk, inf, es in sections:
        if is64:
            sh += struct.pack("<IIQQQQIIQQ", n, t, fl, ad, of, sz, lk, inf, 1, es)
        else:
            sh += struct.pack("<IIIIIIIIII", n, t, fl, ad, of, sz, lk, inf, 1, es)

    ph = b""
    # code and constants, then .data (runs in RAM, loaded from flash)
    for typ, off, vaddr, paddr, filesz, memsz in (
        (1, text_off, text_addr, text_addr, text + rodata, text + rodata),
        (1, data_off, data_addr, data_lma, data, data + bss),
    ):
        if is64:
            ph += struct.pack("<IIQQQQQQ", typ, 5, off, vaddr, paddr, filesz, memsz, 4)
        else:
            ph += struct.pack("<IIIIIIII", typ, off, vaddr, paddr, filesz, memsz, 5, 4)

    ident = b"\x7fELF" + bytes([2 if is64 else 1, 1, 1]) + b"\0" * 9
    if is64:
        header = ident + struct.pack(
            "<HHIQQQIHHHHHH", 2, 0xB7, 1, text_addr, ehsize, shoff, 0, ehsize,
            phentsize, phnum, shentsize, len(sections), len(sections) - 1,
        )
    else:
        header = ident + struct.pack(
            "<HHIIIIIHHHHHH", 2, 0x28, 1, text_addr, ehsize, shoff, 0, ehsize,
            phentsize, phnum, shentsize, len(sections), len(sections) - 1,
        )
    return header + ph + body + sh


def write(path, data, mode="wb"):
    with open(path, mode) as f:
        f.write(data)


def update_dir(root, radio="00 70 0D 08"):
    path = os.path.join(root, "f7-update")
    os.makedirs(path, exist_ok=True)
    write(
        os.path.join(path, "update.fuf"),
        f"Filetype: Flipper firmware upgrade configuration\nVersion: 2\nRadio address: {radio}\nOB reference: AABB\n",
        "w",
    )
    write(os.path.join(path, "firmware.dfu"), b"d" * 1234)
    return path


SYMBOLS = [("big_table", 5000, "object"), ("main_loop", 300, "func"), ("helper", 40, "func")]


class ReadElf(unittest.TestCase):
    def test_elf32_and_elf64_agree(self):
        for is64 in (False, True):
            with tempfile.TemporaryDirectory() as d:
                path = os.path.join(d, "fw.elf")
                write(path, make_elf(800_000, 70_000, 900, 5_000, SYMBOLS, is64=is64))
                elf = size_report.read_elf(path)
                self.assertEqual(elf["symbols"]["big_table"], 5000, is64)
                self.assertEqual(elf["symbols"]["main_loop"], 300, is64)
                names = [s["name"] for s in elf["sections"]]
                self.assertIn(".rodata", names, is64)
                self.assertEqual(elf["symbol_values"]["__heap_start__"], 0x20001000)

    def test_rejects_other_files(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "x")
            write(path, b"not an elf")
            with self.assertRaises(size_report.ElfError):
                size_report.read_elf(path)


def report_for(text=800_000, rodata=70_000, data=900, bss=5_000, radio=RADIO):
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "fw.elf")
        write(path, make_elf(text, rodata, data, bss, SYMBOLS))
        return size_report.build_report(size_report.read_elf(path), radio)


class Report(unittest.TestCase):
    def report(self, **kwargs):
        return report_for(**kwargs)

    def test_image_and_free_flash(self):
        r = self.report()
        # .text + .rodata + the load copy of .data
        self.assertEqual(r["image_size"], 800_000 + 70_000 + 900)
        self.assertEqual(r["free_flash"], (RADIO - FLASH) - r["image_size"])
        self.assertEqual(r["data"], 900)
        self.assertEqual(r["bss"], 5_000)
        self.assertEqual(r["heap"], 0x2002FC00 - 0x20001000)

    def test_free_pages_count_whole_pages_only(self):
        # image ends 1.3 KiB below the radio stack (like dev today): no free page
        r = self.report(text=(RADIO - FLASH) - 1330 - 70_000 - 900)
        self.assertEqual(r["free_flash"], 1330)
        self.assertEqual(r["free_pages"], 0)
        # image ends exactly on a page boundary, two pages below the radio stack
        r = self.report(text=(RADIO - FLASH) - 8192 - 70_000 - 900)
        self.assertEqual(r["free_flash"], 8192)
        self.assertEqual(r["free_pages"], 2)

    def test_ignored_sections(self):
        names = [s["name"] for s in self.report()["sections"]]
        self.assertNotIn(".free_flash", names)
        self.assertNotIn(".symtab", names)
        self.assertEqual({".text", ".rodata", ".data", ".bss"}, set(names))

    def test_radio_address_parsing(self):
        # as scripts/update.py writes it: hex bytes, least significant first
        self.assertEqual(size_report.parse_radio_address("A: 1\nRadio address: 00 70 0D 08\nB: 2\n"), 0x080D7000)
        self.assertEqual(size_report.parse_radio_address("Radio address: 00 70 0D 08"), 0x080D7000)
        self.assertEqual(size_report.parse_radio_address("Radio address: 00 00 0F 08\n"), 0x080F0000)
        with self.assertRaises(ValueError):
            size_report.parse_radio_address("Filetype: x\n")
        with self.assertRaises(ValueError):
            size_report.parse_radio_address("Radio address: 00 00 00 00\n")


class Compare(unittest.TestCase):
    def pair(self):
        before = report_for()
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "fw.elf")
            symbols = [("big_table", 5200, "object"), ("main_loop", 100, "func"), ("new_fn", 90, "func")]
            write(path, make_elf(800_300, 70_000, 900, 5_100, symbols))
            after = size_report.build_report(size_report.read_elf(path), RADIO)
        return before, after

    def test_deltas_and_symbols(self):
        before, after = self.pair()
        delta = size_report.compare(after, before)
        self.assertEqual(delta["image_size"], 300)
        self.assertEqual(delta["free_flash"], -300)
        self.assertEqual(delta["bss"], 100)
        grown = [(c[0], c[3]) for c in delta["symbols_grown"]]
        self.assertEqual(grown[:2], [("big_table", 200), ("new_fn", 90)])
        shrunk = [(c[0], c[3]) for c in delta["symbols_shrunk"]]
        self.assertEqual(shrunk[:2], [("main_loop", -200), ("helper", -40)])
        self.assertEqual(delta["sections"][".text"], 300)


class Markdown(unittest.TestCase):
    def test_symbol_names_cannot_break_out_of_the_table(self):
        self.assertEqual(size_report._code("ok_name"), "`ok_name`")
        evil = "x` | [link](http://evil) |\n@someone `"
        code = size_report._code(evil)
        self.assertNotIn("\n", code)
        self.assertNotIn("|", code)
        self.assertEqual(code.count("`"), 2)
        self.assertLessEqual(len(size_report._code("a" * 500)), 84)

    def test_section_names_cannot_break_out_of_the_table(self):
        evil = ".x` | [link](http://evil) |\n@someone `"
        current = {
            "target": "f7",
            "image_size": 1,
            "free_flash": 1,
            "free_pages": 0,
            "data": 1,
            "bss": 1,
            "heap": None,
            "sections": [{"name": evil, "region": "flash", "size": 1}],
            "symbols": [],
        }
        md = size_report.render_markdown(current, None, [], "nothing", {})
        rows = [line for line in md.splitlines() if "link" in line]
        self.assertEqual(len(rows), 1)
        self.assertTrue(rows[0].startswith("| `"))
        self.assertEqual(rows[0].count("|"), 5)


class Gates(unittest.TestCase):
    BUDGET = {"min_free_flash_bytes": 1000, "max_growth_bytes": 512, "label": "flash-budget"}

    def test_free_flash_gate(self):
        ok = {"free_flash": 1000}
        low = {"free_flash": 999}
        self.assertEqual(size_report.check_gates(ok, None, self.BUDGET, False), [])
        problems = size_report.check_gates(low, None, self.BUDGET, False)
        self.assertEqual(len(problems), 1)
        self.assertIn("999", problems[0])

    def test_growth_gate_and_label(self):
        cur = {"free_flash": 5000}
        self.assertEqual(size_report.check_gates(cur, {"image_size": 512}, self.BUDGET, False), [])
        grown = size_report.check_gates(cur, {"image_size": 513}, self.BUDGET, False)
        self.assertEqual(len(grown), 1)
        self.assertIn("flash-budget", grown[0])
        # with the label the growth is allowed, but the free-flash floor still holds
        self.assertEqual(size_report.check_gates(cur, {"image_size": 4000}, self.BUDGET, True), [])
        self.assertEqual(len(size_report.check_gates({"free_flash": 10}, {"image_size": 4000}, self.BUDGET, True)), 1)

    def test_no_baseline_means_no_growth_check(self):
        self.assertEqual(size_report.check_gates({"free_flash": 5000}, None, self.BUDGET, False), [])

    def test_shrinking_is_fine(self):
        self.assertEqual(size_report.check_gates({"free_flash": 5000}, {"image_size": -2000}, self.BUDGET, False), [])

    def test_repo_budget_file_is_valid(self):
        with open(os.path.join(ROOT, ".github", "size_budget.json")) as f:
            budget = json.load(f)
        self.assertGreater(budget["min_free_flash_bytes"], 0)
        self.assertEqual(budget["max_growth_bytes"], 512)
        self.assertEqual(budget["label"], "flash-budget")


class CommandLine(unittest.TestCase):
    def run_cli(self, d, extra=(), text=800_000, budget=None):
        elf = os.path.join(d, "fw.elf")
        write(elf, make_elf(text, 70_000, 900, 5_000, SYMBOLS))
        bpath = os.path.join(d, "budget.json")
        write(bpath, json.dumps(budget or {"min_free_flash_bytes": 1000, "max_growth_bytes": 512}), "w")
        out_json = os.path.join(d, "size.json")
        out_md = os.path.join(d, "size.md")
        code = size_report.main(
            ["--elf", elf, "--update-dir", update_dir(d), "--budget", bpath,
             "--out-json", out_json, "--out-md", out_md, *extra]
        )
        return code, out_json, out_md

    def test_pass_then_compare_with_its_own_output(self):
        with tempfile.TemporaryDirectory() as d:
            code, out_json, out_md = self.run_cli(d)
            self.assertEqual(code, 0)
            with open(out_md) as f:
                md = f.read()
            self.assertTrue(md.startswith(size_report.MARKER))
            self.assertIn("Free flash below radio stack", md)
            self.assertIn("nothing (no earlier build", md)
            with open(out_json) as f:
                saved = json.load(f)
            self.assertEqual(saved["dfu_size"], 1234)
            # next build: 600 B bigger, compared with the first one
            code, _, out_md = self.run_cli(d, ["--baseline", out_json, "--baseline-note", "dev abc"], text=800_600)
            self.assertEqual(code, 1)
            with open(out_md) as f:
                md = f.read()
            self.assertIn("Failed", md)
            self.assertIn("grew by 600 B", md)
            self.assertIn("dev abc", md)
            code, _, _ = self.run_cli(d, ["--baseline", out_json, "--growth-allowed"], text=800_600)
            self.assertEqual(code, 0)

    def test_free_flash_failure_exit_code(self):
        with tempfile.TemporaryDirectory() as d:
            code, _, md = self.run_cli(d, text=(RADIO - FLASH) - 70_000 - 900 - 500)
            self.assertEqual(code, 1)
            with open(md) as f:
                self.assertIn("below the minimum", f.read())


SAMPLE_MAP = """\
Discarded input sections

 .text          0x0000000008000000        0x0 build/f7-firmware-C/lib/foo.o

Linker script and memory map

.text           0x0000000008000000     0x2000
 *(.text*)
 .text.nfc_a    0x0000000008000000      0x100 build/f7-firmware-C/lib/libnfc.a(nfc.o)
 .text.nfc_b    0x0000000008000100      0x200 build/f7-firmware-C/lib/libnfc.a(nfc_b.o)
 .text.very_long_section_name_that_wraps
                0x0000000008000300      0x40 /opt/toolchain/arm-none-eabi/lib/thumb/v7e-m+fp/hard/libc_nano.a(lib_a-strtod.o)
 *fill*         0x0000000008000340        0x4
 .text.main     0x0000000008000344       0x80 build/f7-firmware-C/applications/services/gui/gui.o
.free_flash     0x00000000080d6d68    0x29000
 *fill*         0x00000000080d6d68    0x29000
._user_heap_stack
                0x0000000020001000    0x2e000
 *fill*         0x0000000020001000    0x2e000
MB_MEM1         0x0000000020030000      0x10 (NOLOAD)
 MB_MEM1        0x0000000020030000      0x10 build/f7-firmware-C/lib/libnfc.a(nfc.o)
.bss            0x0000000020000000      0x400
 .bss.x         0x0000000020000000      0x400 build/f7-firmware-C/lib/libnfc.a(nfc.o)
.data
                0x0000000020000400       0x20 load address 0x00000000080003c4
 .data.y        0x0000000020000400       0x20 build/f7-firmware-C/applications/services/gui/gui.o

Cross Reference Table

Symbol                                            File
_strtod_l                                         /opt/x/libc_nano.a(lib_a-strtod.o)
                                                  build/f7-firmware-C/lib/libnfc.a(nfc.o)
"""


class LinkMapTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location(
            "linkmap", os.path.join(ROOT, "scripts", "linkmap.py")
        )
        self.linkmap = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.linkmap)

    def test_groups(self):
        sections = self.linkmap.parse_map(SAMPLE_MAP.splitlines(True))
        groups, objects = self.linkmap.summarize(sections)
        self.assertEqual(groups["libnfc.a"], 0x300)
        self.assertEqual(groups["libc_nano.a"], 0x40)
        self.assertEqual(groups["(padding)"], 4)
        # the loose object's .text and the load copy of its .data
        self.assertEqual(groups["applications/services/gui"], 0x80 + 0x20)
        self.assertNotIn("(toolchain)", "".join(groups))
        self.assertEqual(objects["libnfc.a:nfc_b.o"], 0x200)

    def test_loose_object_name_is_not_repeated(self):
        sections = self.linkmap.parse_map(SAMPLE_MAP.splitlines(True))
        _, objects = self.linkmap.summarize(sections)
        self.assertIn("applications/services/gui/gui.o", objects)
        self.assertNotIn("applications/services/gui:applications/services/gui/gui.o", objects)

    def test_wrapped_section_name_without_a_dot(self):
        text = """\
Linker script and memory map

.text           0x0000000008000000      0x100
 CUSTOM_SECTION_WITH_A_LONG_NAME_THAT_WRAPS
                0x0000000008000000      0x100 build/f7-firmware-C/lib/libfoo.a(foo.o)
"""
        sections = self.linkmap.parse_map(text.splitlines(True))
        self.assertEqual([(s["section"], s["size"]) for s in sections],
                         [("CUSTOM_SECTION_WITH_A_LONG_NAME_THAT_WRAPS", 0x100)])

    def test_noload_sections_and_ram_buffers_are_not_flash(self):
        sections = self.linkmap.parse_map(SAMPLE_MAP.splitlines(True))
        self.assertFalse([s for s in sections if s["output"] == ".free_flash"])
        self.assertFalse([s for s in sections if s["section"] == "MB_MEM1"])

    def test_bss_is_not_flash(self):
        sections = self.linkmap.parse_map(SAMPLE_MAP.splitlines(True))
        self.assertFalse([s for s in sections if s["section"] == ".bss.x"])

    def test_cref(self):
        cref = self.linkmap.parse_cref(SAMPLE_MAP.splitlines(True))
        defined, users = cref["_strtod_l"]
        self.assertIn("libc_nano.a", defined)
        self.assertEqual(len(users), 1)

    def test_report_with_map_and_baseline(self):
        lines = SAMPLE_MAP.splitlines(True)
        base = {"target": "f7", "image_size": 0, "free_flash": 0, "free_pages": 0, "data": 0, "bss": 0,
                "sections": [], "symbols": {}}
        size_report.add_map(base, lines[:9])  # without gui.o
        now = dict(base)
        size_report.add_map(now, lines)
        self.assertEqual(now["flash_groups"]["libnfc.a"], 0x300)
        delta = size_report.compare(now, base)
        self.assertTrue(delta["has_group_baseline"])
        self.assertEqual(delta["flash_groups"]["applications/services/gui"], 0xA0)
        md = size_report.render_markdown(now, delta, [], "x", {})
        self.assertIn("Flash by library or directory", md)
        self.assertIn("`libnfc.a`", md)
        self.assertIn("+160", md)
        # a library that is gone still shows, at 0 B
        gone = dict(base)
        size_report.add_map(gone, lines)
        gone["flash_groups"] = dict(gone["flash_groups"], **{"libgone.a": 5000})
        md = size_report.render_markdown(now, size_report.compare(now, gone), [], "x", {})
        self.assertIn("`libgone.a` | 0 |", md)
        self.assertIn("-5,000", md)
        # a baseline from before the map existed gives "n/a", not a huge change
        old = {k: v for k, v in base.items() if k != "flash_groups"}
        md = size_report.render_markdown(now, size_report.compare(now, old), [], "x", {})
        self.assertNotIn("+768", md)


class MapGuardTest(unittest.TestCase):
    def test_gap_warning(self):
        report = {"flash_map_check": {"map": 1_250_000, "elf": 880_000}}
        self.assertIn("probably", size_report.map_gap_warning(report))
        report = {"flash_map_check": {"map": 890_000, "elf": 880_000}}
        self.assertIsNone(size_report.map_gap_warning(report))
        self.assertIsNone(size_report.map_gap_warning({}))

    def test_bad_map_does_not_fail_the_report(self):
        with tempfile.TemporaryDirectory() as d:
            elf = os.path.join(d, "fw.elf")
            write(elf, make_elf(800_000, 70_000, 900, 5_000, SYMBOLS))
            upd = update_dir(d)
            budget = os.path.join(d, "budget.json")
            write(budget, json.dumps({"min_free_flash_bytes": 0, "max_growth_bytes": 0}), "w")
            bad = os.path.join(d, "map")
            write(bad, b"Linker script and memory map\n.text 0xZZ\n")
            out_json, out_md = os.path.join(d, "o.json"), os.path.join(d, "o.md")
            with mock.patch.object(size_report.linkmap, "parse_map", side_effect=ValueError("boom")):
                code = size_report.main(
                    ["--elf", elf, "--update-dir", upd, "--budget", budget,
                     "--map", bad, "--out-json", out_json, "--out-md", out_md]
                )
            self.assertEqual(code, 0)
            self.assertTrue(os.path.exists(out_md))
            self.assertNotIn("flash_groups", json.load(open(out_json)))


if __name__ == "__main__":
    unittest.main()
