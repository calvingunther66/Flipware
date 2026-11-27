#pragma once
// Host shim: the helper macros of furi/core/common_defines.h that parsers and protocols use.
#include <stddef.h>
#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif
#ifndef COUNT_OF
#define COUNT_OF(x) (sizeof(x) / sizeof(x[0]))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef FURI_PACKED
#define FURI_PACKED __attribute__((packed))
#endif
