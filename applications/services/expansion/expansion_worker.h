/**
 * @file expansion_worker.h
 * @brief Expansion module handling thread wrapper.
 *
 * The worker is started each time an expansion module is detected
 * and handles all of the communication protocols. Likewise, it is stopped
 * upon module disconnection or communication error.
 *
 * @warning This file is a private implementation detail. Please do not attempt to use it in applications.
 */
#pragma once

#include <furi_hal_serial_types.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "expansion.h"

/**
 * @brief Expansion worker opaque type declaration.
 */
typedef struct ExpansionWorker ExpansionWorker;

typedef enum {
    ExpansionWorkerCallbackReasonExit,
    ExpansionWorkerCallbackReasonConnected,
} ExpansionWorkerCallbackReason;

/**
 * @brief Worker callback type.
 *
 * @see expansion_worker_set_callback()
 *
 * @param[in,out] context pointer to a user-defined object.
 * @param[in] reason reason for the callback.
 */
typedef void (*ExpansionWorkerCallback)(void* context, ExpansionWorkerCallbackReason reason);

// Coprocessor inbound-message callback: @see ExpansionCoprocCallback in expansion.h
// (the type is defined there since it's part of the public API surface too;
// fires on the worker's own thread here, only while a module is connected).

/**
 * @brief Create an expansion worker instance.
 *
 * @param[in] serial_id numerical identifier of the serial to be used by the worker.
 * @returns pointer to the created instance.
 */
ExpansionWorker* expansion_worker_alloc(FuriHalSerialId serial_id);

/**
 * @brief Delete an expansion worker instance.
 *
 * @param[in,out] instance pointer to the instance to be deleted.
 */
void expansion_worker_free(ExpansionWorker* instance);

/**
 * @brief Set the module disconnect callback.
 *
 * The callback will be triggered upon worker stop EXCEPT
 * when it was stopped via an expansion_worker_stop() call.
 *
 * In other words, the callback will ONLY be triggered if the worker was
 * stopped due to the user disconnecting/resetting/powering down the module,
 * or due to some communication error.
 *
 * @param[in,out] instance pointer to the worker instance to be modified.
 * @param[in] callback pointer to the callback function to be called under the above conditions.
 * @param[in] context pointer to a user-defined object, will be passed as a parameter to the callback.
 */
void expansion_worker_set_callback(
    ExpansionWorker* instance,
    ExpansionWorkerCallback callback,
    void* context);

/**
 * @brief Start the expansion module worker.
 *
 * @param[in,out] instance pointer to the worker instance to be started.
 */
void expansion_worker_start(ExpansionWorker* instance);

/**
 * @brief Stop the expansion module worker.
 *
 * If the worker was stopped via this call (and not because of module disconnect/
 * protocol error), the callback will not be triggered.
 *
 * @param[in,out] instance pointer to the worker instance to be stopped.
 */
void expansion_worker_stop(ExpansionWorker* instance);

/**
 * @brief Set the coprocessor inbound-message callback.
 *
 * Must be called before expansion_worker_start(). The Coproc channel is
 * accepted independently of RPC session state (i.e. in both the Connected
 * and RpcActive worker states) once the handshake has completed.
 *
 * @param[in,out] instance pointer to the worker instance to be modified.
 * @param[in] callback pointer to the callback function.
 * @param[in] context pointer to a user-defined object, passed to the callback.
 */
void expansion_worker_set_coproc_callback(
    ExpansionWorker* instance,
    ExpansionCoprocCallback callback,
    void* context);

/**
 * @brief Send a coprocessor job request to the connected module.
 *
 * Blocks the calling thread while chunking and transmitting the request,
 * waiting for a per-chunk Status acknowledgement from the module (same
 * flow-control discipline as the RPC send path). Safe to call from any
 * thread; only one coprocessor send may be in flight at a time per worker.
 *
 * @param[in,out] instance pointer to the worker instance.
 * @param[in] job_id identifier for this job; the matching result delivered to
 *            the coproc callback will carry the same value.
 * @param[in] task_type task type identifier, meaningful to both firmwares above this layer.
 * @param[in] data pointer to the job input payload.
 * @param[in] data_size size of the job input payload, in bytes.
 * @returns true if the request was fully transmitted and acknowledged, false on any I/O error.
 */
bool expansion_worker_coproc_send(
    ExpansionWorker* instance,
    uint16_t job_id,
    uint8_t task_type,
    const uint8_t* data,
    size_t data_size);
