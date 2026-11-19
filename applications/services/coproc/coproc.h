/**
 * @file coproc.h
 * @brief Compute-offload coprocessor service.
 *
 * Dispatches small, well-defined tasks to a connected expansion module's own
 * microcontroller (e.g. the Video Game Module's RP2040) over the expansion
 * link's Coproc channel (@see expansion_protocol.h), with automatic fallback
 * to a locally-registered software implementation whenever no module is
 * connected or the task isn't supported.
 *
 * @warning Read this before reaching for it as a performance tool: the link
 * is a ~1.8 Mbaud stop-and-wait UART, roughly 20-100x slower per byte than
 * running the same work on the Flipper's own CPU for straightforward
 * per-byte tasks (CRC, hashing, compression). It only pays off for tasks
 * with a small job/result and a large amount of compute per byte
 * transferred (e.g. keyspace search). CoprocTaskCrc32 in particular is
 * wired up purely to exercise the pipeline end-to-end, not as a speed win.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief FURI record key to access the coproc object.
 */
#define RECORD_COPROC "coproc"

/**
 * @brief Coproc opaque type declaration.
 */
typedef struct Coproc Coproc;

/**
 * @brief Enumeration of supported task types.
 *
 * A connected module is assumed to support every task type in this
 * enumeration (there is no runtime capability query yet - a natural
 * follow-up once more task types exist and modules may differ).
 */
typedef enum {
    CoprocTaskCrc32, /**< CRC-32, IEEE 802.3 polynomial. Pipeline smoke test; not a speed win, see warning above. */
    CoprocTaskCompressHeatshrink, /**< Heatshrink LZSS compression. */
    CoprocTaskDecompressHeatshrink, /**< Heatshrink LZSS decompression. */
    CoprocTaskCount,
} CoprocTaskType;

/**
 * @brief Local fallback implementation function type.
 *
 * Same buffer-in/buffer-out shape as the offloaded task itself.
 *
 * @param[in] input pointer to the task input.
 * @param[in] input_size size of the task input, in bytes.
 * @param[out] output pointer to the caller-allocated output buffer.
 * @param[in,out] output_size capacity of the output buffer on entry, actual result size on exit.
 * @param[in,out] context pointer to a user-defined object, as passed to coproc_register_fallback().
 * @returns true on success.
 */
typedef bool (*CoprocFallbackFn)(
    const uint8_t* input,
    size_t input_size,
    uint8_t* output,
    size_t* output_size,
    void* context);

/**
 * @brief Check whether an expansion module is connected and reachable for offload.
 *
 * @param[in,out] instance pointer to the Coproc instance.
 * @returns true if a module is connected.
 */
bool coproc_is_available(Coproc* instance);

/**
 * @brief Check whether the connected module (if any) supports the given task.
 *
 * @param[in,out] instance pointer to the Coproc instance.
 * @param[in] task task type to check.
 * @returns true if a module is connected and would be attempted for this task.
 */
bool coproc_supports(Coproc* instance, CoprocTaskType task);

/**
 * @brief Register (or replace) the local software fallback for a task type.
 *
 * @param[in,out] instance pointer to the Coproc instance.
 * @param[in] task task type to register a fallback for.
 * @param[in] fn pointer to the fallback function, or NULL to unregister.
 * @param[in] context pointer to a user-defined object, passed to the fallback function.
 */
void coproc_register_fallback(
    Coproc* instance,
    CoprocTaskType task,
    CoprocFallbackFn fn,
    void* context);

/**
 * @brief Run a task, offloading to the connected module if possible.
 *
 * Blocks the calling thread. If a module is connected, attempts to offload
 * the task and waits up to timeout_ms for a result; on any failure (no
 * module, send error, timeout, or the module reporting an error) falls back
 * to the registered local implementation for this task, if any, with no
 * further time limit of its own.
 *
 * @param[in,out] instance pointer to the Coproc instance.
 * @param[in] task task type to run.
 * @param[in] input pointer to the task input.
 * @param[in] input_size size of the task input, in bytes.
 * @param[out] output pointer to the caller-allocated output buffer.
 * @param[in,out] output_size capacity of the output buffer on entry, actual result size on exit.
 * @param[in] timeout_ms maximum time to wait for an offloaded result before falling back, in milliseconds.
 * @param[out] was_offloaded if non-NULL, set to true if this specific call actually completed via the
 * module, false if it used the local fallback (or failed before reaching either) - reflects what
 * happened for THIS call, not just current module connectivity.
 * @returns true on success (offloaded or via fallback), false if both were unavailable or failed.
 */
bool coproc_run(
    Coproc* instance,
    CoprocTaskType task,
    const uint8_t* input,
    size_t input_size,
    uint8_t* output,
    size_t* output_size,
    uint32_t timeout_ms,
    bool* was_offloaded);

#ifdef __cplusplus
}
#endif
