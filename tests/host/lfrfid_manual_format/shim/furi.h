// Host shim: the bits of <furi.h> the RFID helpers and protocols use
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <core/check.h>
#include <core/string.h>
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
#define FURI_LOG_D(...)
#define FURI_LOG_I(...)
#define FURI_LOG_E(...)
#define FURI_LOG_W(...)
// furi's malloc hands out zeroed memory and protocols rely on it
#define malloc(size) calloc(1, (size))
