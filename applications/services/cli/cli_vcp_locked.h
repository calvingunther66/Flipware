/**
 * @file cli_vcp_locked.h
 * Firmware-internal (not SDK) additions to the VCP CLI for host sign-in.
 */
#pragma once

#include "cli_vcp.h"
#include <toolbox/cli/cli_registry.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Enable the USB CLI in locked mode: the shell only offers the commands in the
 * locked registry, and loads nothing from the SD card. Calling cli_vcp_enable()
 * afterwards switches back to the full CLI; the shell restarts in place.
 */
void cli_vcp_enable_locked(CliVcp* cli_vcp);

/** Registry of commands reachable while the device is locked. */
CliRegistry* cli_vcp_get_locked_registry(CliVcp* cli_vcp);

#ifdef __cplusplus
}
#endif
