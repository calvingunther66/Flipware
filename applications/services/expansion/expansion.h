/**
 * @file expansion.h
 * @brief Expansion module support library.
 */
#pragma once

#include <furi_hal_serial_types.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief FURI record key to access the expansion object.
 */
#define RECORD_EXPANSION "expansion"

/**
 * @brief Expansion opaque type declaration.
 */
typedef struct Expansion Expansion;

/**
 * @brief Coprocessor inbound-message callback type.
 *
 * Invoked once a complete coprocessor job *result* message has been
 * reassembled from the connected module. Runs on an internal expansion
 * service thread — keep it fast, and do not call back into expansion.h
 * functions from within it.
 *
 * @param[in,out] context pointer to a user-defined object.
 * @param[in] job_id identifier as passed to expansion_coproc_send() for this job.
 * @param[in] is_error true if the module reported failure; data/data_size are undefined in that case.
 * @param[in] data pointer to the reassembled result payload. Only valid for the duration of the callback.
 * @param[in] data_size size of the reassembled result payload, in bytes.
 */
typedef void (*ExpansionCoprocCallback)(
    void* context,
    uint16_t job_id,
    bool is_error,
    const uint8_t* data,
    size_t data_size);

/**
 * @brief Enable support for expansion modules.
 *
 * Calling this function will load user settings and enable
 * expansion module support on the serial port specified in said settings.
 *
 * If expansion module support was disabled in settings, this function
 * does nothing.
 *
 * @param[in,out] instance pointer to the Expansion instance.
 */
void expansion_enable(Expansion* instance);

/**
 * @brief Disable support for expansion modules.
 *
 * Calling this function will cease all communications with the
 * expansion module (if any), release the serial handle and
 * reset the respective pins to the default state.
 *
 * @note Applications requiring serial port access MUST call
 * this function BEFORE calling furi_hal_serial_control_acquire().
 * Similarly, an expansion_enable() call MUST be made right AFTER
 * a call to furi_hal_serial_control_release() to ensure that
 * the user settings are properly restored.
 *
 * @param[in,out] instance pointer to the Expansion instance.
 */
void expansion_disable(Expansion* instance);

/**
 * @brief Check if an expansion module is connected.
 *
 * @param[in,out] instance pointer to the Expansion instance.
 *
 * @returns true if the module is connected and initialized, false otherwise.
 */
bool expansion_is_connected(Expansion* instance);

/**
 * @brief Enable support for expansion modules on designated serial port.
 *
 * Only one serial port can be used to communicate with an expansion
 * module at a time.
 *
 * Calling this function when expansion module support is already enabled
 * will first disable the previous setting, then enable the current one.
 *
 * @warning This function does not respect user settings for expansion modules,
 * so calling it might leave the system in inconsistent state. Avoid using it
 * unless absolutely necessary.
 *
 * @param[in,out] instance pointer to the Expansion instance.
 * @param[in] serial_id numerical identifier of the serial.
 */
void expansion_set_listen_serial(Expansion* instance, FuriHalSerialId serial_id);

/**
 * @brief Register the coprocessor inbound-message callback.
 *
 * Only one callback may be registered at a time; a later call replaces the
 * previous registration. The callback applies to any module connection
 * established after this call (and the currently active one, if any).
 *
 * @param[in,out] instance pointer to the Expansion instance.
 * @param[in] callback pointer to the callback function, or NULL to unregister.
 * @param[in] context pointer to a user-defined object, passed to the callback.
 */
void expansion_set_coproc_callback(
    Expansion* instance,
    ExpansionCoprocCallback callback,
    void* context);

/**
 * @brief Submit a coprocessor job request to the connected module.
 *
 * Blocks the calling thread for the duration of the (chunked) transfer.
 * Only one call may be in flight at a time across all callers — concurrent
 * callers will be serialized in an unspecified order; coproc.c enforces this
 * by only ever calling from its own single service thread.
 *
 * @param[in,out] instance pointer to the Expansion instance.
 * @param[in] job_id identifier for this job; the matching result delivered to
 *            the coproc callback will carry the same value.
 * @param[in] task_type task type identifier, meaningful to both firmwares above this layer.
 * @param[in] data pointer to the job input payload.
 * @param[in] data_size size of the job input payload, in bytes.
 * @returns true if the request was fully transmitted and acknowledged, false
 *          if no module is connected or on any I/O error.
 */
bool expansion_coproc_send(
    Expansion* instance,
    uint16_t job_id,
    uint8_t task_type,
    const uint8_t* data,
    size_t data_size);

#ifdef __cplusplus
}
#endif
