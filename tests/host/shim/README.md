# Host shim

A small stand-in for `<furi.h>` so firmware code that does not touch hardware can
be built with the host compiler and tested under AddressSanitizer and
UndefinedBehaviorSanitizer.

Use it from a test's `run.sh` with `-I tests/host/shim`, ahead of `-I furi`. The
shim's `furi.h`, `core/check.h`, `core/common_defines.h`, `core/log.h` and
`core/memmgr.h` replace the firmware's; `core/string.h` and the rest still come
from `furi/`.

- `furi_check`, `furi_assert` and `furi_crash` print the location and `abort()`.
- `FURI_LOG_*` compile to nothing.
- `malloc` zeroes memory, as furi's does.

If the code under test needs something else from furi, add it here, as a no-op
or a plain libc equivalent. Code that needs the FreeRTOS kernel, a HAL or a
driver does not belong in a host test: use the emulator checks instead.
