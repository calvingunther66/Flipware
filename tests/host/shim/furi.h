// Host shim: the bits of <furi.h> that parsers, protocols and helpers use when
// they are built on a PC. Shared by every test under tests/host.
// Add a macro or a header here only when firmware code under test needs it, and keep
// it a no-op or a plain libc equivalent: nothing in this directory may need hardware.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <core/check.h>
#include <core/common_defines.h>
#include <core/log.h>
#include <core/memmgr.h>
#include <core/string.h>
