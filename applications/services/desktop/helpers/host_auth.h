/**
 * @file host_auth.h
 * Host sign-in: lets an enrolled computer unlock the PIN lock over USB with a
 * device-bound passkey (WebAuthn assertion from a Mac Secure Enclave key,
 * Windows Hello or a FIDO2 security key). See documentation/HostAuth.md.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <storage/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Enrolled public keys (MAC-protected, epoch-bound to an RTC register) */
#define HOST_AUTH_KEYS_PATH     INT_PATH(".hostauth.keys")
#define HOST_AUTH_KEYS_TMP_PATH INT_PATH(".hostauth.tmp")

/** Register the `hostauth` CLI command in the main and the locked-mode registries. */
void host_auth_init(void);

/**
 * True if a key file exists. Cheap (no crypto), used to decide whether to keep
 * the USB CLI up in locked mode. The file's integrity is checked when it is used.
 */
bool host_auth_is_enrolled(void);

/** Called on every unlock (PIN or host): clears the host sign-in backoff. */
void host_auth_on_unlock(void);

/**
 * Forget every enrolled computer: invalidates every copy of the key file
 * (including ones in updater backups) by changing the epoch register, then
 * deletes the file. Returns true if no key file is left.
 */
bool host_auth_forget_all(void);

/* Internal, shared by host_auth.c and host_auth_state.c */
uint32_t host_auth_new_epoch(uint32_t avoid);
void host_auth_backoff_clear(void);

#ifdef __cplusplus
}
#endif
