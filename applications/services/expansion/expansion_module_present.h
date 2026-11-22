#pragma once
/*
 * Lock-free "is a module attached" query for other firmware services.
 *
 * Unlike expansion_is_connected(), this does not round-trip through the
 * expansion control thread, so it is safe to call from the storage thread
 * (the expansion thread itself waits on storage when it loads settings).
 * Firmware-internal: not part of the app SDK.
 */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** True from module detection until the module disconnects. */
bool expansion_is_module_present(void);

#ifdef __cplusplus
}
#endif
