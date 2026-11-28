# Link-time optimization

The compact base firmware (`COMPACT=1`, the default) is built with GCC link-time
optimization (LTO). It saves about 15 KB of flash (roadmap 2.2, #46): the
compiler sees the whole program at once, so it inlines across files and drops what
nothing uses.

`./fbt LTO=0` builds without it, for a size or debug comparison, or to rule LTO out
when something odd happens. The build takes about four minutes longer with LTO on.

## What stays out, and why

Everything lives in [`scripts/fbt/lto.py`](../scripts/fbt/lto.py), and
`tests/host/lto_lists` checks it against the source tree.

- **External apps (`.fap`).** They are linked with `-Ur`, which cannot take LTO
  objects, so the LTO flags are removed from the app build (`site_scons/extapps.scons`).
- **Libraries an app can link in (`fap_libs`).** `assets`, `toolbox`, `mbedtls`,
  `infrared`, `u8g2`, `hwdrivers`, `music_worker` and `ble_profile` are built without
  LTO, for the same reason. Add a library to `fap_libs` in an `application.fam` and the
  host test fails until it is also in `NO_LTO_LIBS`. Without the test the app link
  fails with "incremental linking of LTO and non-LTO objects".
- **`furi/core/check.c`.** The crash handler loads addresses with `ldr rX, =symbol` in
  inline asm. LTO moves the literal pool out of range and the assembler stops with
  "offset out of range". It is built with `-fno-lto`.

## Things LTO can silently drop

- **`--wrap` replacements.** libc calls `printf`, `malloc` and others, the linker
  renames those to `__wrap_*`, and the LTO plugin does not see the call, so it would
  throw the replacement away as unused. `-Wl,--undefined=__wrap_<name>` keeps each one;
  the names are `WRAPPERS` in `lto.py`. The host test compares them with the functions
  defined in `lib/print/wrappers.c` and `furi/core/memmgr.c`.
- **The interrupt vector table.** Only the linker script (`KEEP(*(.isr_vector))`) refers
  to `reset_vector`, so LTO removed it and the firmware did not start. It is marked
  `used` in `targets/f7/src/stm32wb55_startup.c`. A table or handler that only the linker
  script or inline asm refers to needs the same.

## Reading the size report

The linker map no longer says which library a function came from, because LTO
compiles the code again at link time in partitions with random temporary names. The
report puts them in one row, `(LTO partitions)`. For a per-library comparison build with
`LTO=0`. The `.isr_vector` row in the section table has to be there: if it is missing, the
firmware will not boot.

## Checks

A build with LTO has to pass the emulator checks (smoke, ble, lock, uart, fwbugs, update,
vgm) and a boot on a real Flipper. Crash and `furi_check` behavior, interrupts and the
radio stack are the places a miscompile would show first.
