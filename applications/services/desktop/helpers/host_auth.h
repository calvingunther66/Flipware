/**
 * @file host_auth.h
 * Host sign-in: lets an enrolled computer unlock the PIN lock over USB by
 * signing a one-time challenge with a P-256 key (on a Mac, kept in the
 * Secure Enclave). See documentation/HostAuth.md.
 */
#pragma once

#include <stdbool.h>
#include <storage/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Enrolled public keys (MAC-protected, kept across updates via the /int backup) */
#define HOST_AUTH_KEYS_PATH INT_PATH(".hostauth.keys")

/** Register the `hostauth` CLI command in the main and the locked-mode registries. */
void host_auth_init(void);

/**
 * True if a key file exists. Cheap (no crypto), used to decide whether to keep
 * the USB CLI up in locked mode. The file's integrity is checked when it is used.
 */
bool host_auth_is_enrolled(void);

#ifdef __cplusplus
}
#endif
