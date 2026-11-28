"""Link-time optimization settings of the firmware build (roadmap 2.2).

One place for everything that has to agree: firmware.scons turns the flags on,
fbt_hwtarget.py and extapps.scons keep them out of what the apps link, and
tests/host/lto_lists checks these lists against the source tree. See
documentation/LinkTimeOptimization.md.
"""

CCFLAGS = ["-flto", "-ffat-lto-objects"]
LINKFLAGS = ["-flto", "-fuse-linker-plugin"]
ALL_FLAGS = ("-flto", "-ffat-lto-objects", "-fuse-linker-plugin")

# Libraries an external app can link into itself (`fap_libs` in an application.fam).
# The apps are linked with -Ur, which cannot take LTO objects, so these stay plain.
# A new fap_lib that is missing here fails the test in tests/host/lto_lists.
NO_LTO_LIBS = (
    "assets",
    "ble_profile",
    "hwdrivers",
    "infrared",
    "mbedtls",
    "music_worker",
    "toolbox",
    "u8g2",
)

# Sources that cannot be link-time optimized: furi/core/check.c loads addresses
# with `ldr rX, =symbol` in inline asm, and LTO moves the literal pool out of range.
NO_LTO_SOURCES = ("furi/core/check.c",)

# Functions the firmware provides as `__wrap_<name>` (lib/print/wrappers.c and
# furi/core/memmgr.c). The libc objects call them through the linker's --wrap,
# which the LTO plugin cannot see, so it would drop them as unused. A name missing
# here fails the link with "undefined reference to __wrap_<name>".
WRAPPERS = (
    "_calloc_r",
    "_free_r",
    "_malloc_r",
    "_realloc_r",
    "fflush",
    "fgetc",
    "fgets",
    "getc",
    "getchar",
    "printf",
    "putc",
    "putchar",
    "puts",
    "snprintf",
    "ungetc",
    "vsnprintf",
)
