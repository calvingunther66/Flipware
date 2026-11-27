#pragma once
// Host shim: furi's malloc hands out zeroed memory and protocols rely on it.
// Include this after <stdlib.h>, as furi.h does.
#include <stdlib.h>
#define malloc(size) calloc(1, (size))
