#include "expansion_worker.h"

#include <power/power_service/power.h>
#include <furi_hal_power.h>

#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>

#include <furi.h>
#include <rpc/rpc.h>

#include "expansion_protocol.h"

#define TAG "ExpansionSrv"

// Was 768 bytes, sized for the original shallow call depth (frame
// decode/encode only). Servicing outbound coprocessor sends now happens on
// this same thread (expansion_worker_service_coproc_tx ->
// expansion_worker_coproc_send_now -> expansion_worker_send_frame ->
// expansion_protocol_encode), nested inside the existing receive-callback
// chain - a real stack watermark warning (248 bytes free) was observed at
// 768, immediately followed by the worker repeatedly restarting. Bumped
// with real headroom rather than the minimum that clears the warning.
#define EXPANSION_WORKER_STACK_SZIE  (2048UL)
#define EXPANSION_WORKER_BUFFER_SIZE (sizeof(ExpansionFrame) + sizeof(ExpansionFrameChecksum))

/**
 * @brief Maximum reassembled size of a single coprocessor job message.
 *
 * Applies independently to requests (outbound) and results (inbound); only
 * one inbound result and one outbound request are ever reassembled/in-flight
 * at a time per worker, so a single buffer of this size is kept for RX.
 */
#define EXPANSION_WORKER_COPROC_MAX_MESSAGE_SIZE (4096U)

typedef enum {
    ExpansionWorkerStateHandShake,
    ExpansionWorkerStateConnected,
    ExpansionWorkerStateRpcActive,
} ExpansionWorkerState;

typedef enum {
    ExpansionWorkerExitReasonUnknown,
    ExpansionWorkerExitReasonUser,
    ExpansionWorkerExitReasonError,
    ExpansionWorkerExitReasonTimeout,
} ExpansionWorkerExitReason;

typedef enum {
    ExpansionWorkerFlagStop = 1 << 0,
    ExpansionWorkerFlagData = 1 << 1,
    ExpansionWorkerFlagError = 1 << 2,
    ExpansionWorkerFlagCoprocTx = 1 << 3,
} ExpansionWorkerFlag;

#define EXPANSION_ALL_FLAGS                                                       \
    (ExpansionWorkerFlagData | ExpansionWorkerFlagStop | ExpansionWorkerFlagError | \
     ExpansionWorkerFlagCoprocTx)

// Outbound coprocessor job request, submitted by an arbitrary caller thread
// (via expansion_worker_coproc_send()) and always executed by the worker's
// own thread - never touch tx_semaphore or the UART handle from any other
// thread; both are owned exclusively by the worker thread for their entire
// lifetime, and reading/using them from elsewhere raced against the worker's
// own connect/teardown handling (the root cause of two prior full-device
// hangs, since a foreign thread could be mid-way through a raw busy-wait
// UART TX or blocked acquiring a semaphore the worker thread had just freed
// out from under it).
typedef struct {
    uint16_t job_id;
    uint8_t task_type;
    const uint8_t* data;
    size_t data_size;
    bool success;
    FuriSemaphore* done;
} ExpansionCoprocTxRequest;

struct ExpansionWorker {
    FuriThread* thread;
    FuriStreamBuffer* rx_buf;
    FuriMutex* tx_mutex;

    // Single-slot handoff for outbound coprocessor job requests; capacity 1
    // is sufficient because callers are already serialized one-job-at-a-time
    // by coproc_run()'s own mutex. Only ever drained by the worker thread.
    FuriMessageQueue* coproc_tx_queue;

    // Shared outbound ack gate: the link permits only one un-acked chunked
    // send in flight at a time, regardless of whether it belongs to the RPC
    // (Data frame) or coprocessor (Coproc frame) channel. Allocated once the
    // handshake completes (entering Connected state), freed on worker exit -
    // its lifetime is NOT tied to the RPC session, since Coproc sends are
    // valid whether or not RPC is active.
    FuriSemaphore* tx_semaphore;

    // Multi-chunk outbound coproc send state. A send is driven one chunk at
    // a time, ack-by-ack, from the Status-frame branches below - never by
    // blocking on tx_semaphore in a loop, since that would block the only
    // thread that can ever process the ack that releases it (a self-deadlock
    // hit and root-caused this session: chunk 1 always succeeded because
    // tx_semaphore starts available, chunk 2 always timed out because
    // nothing could ever release it back). NULL when idle.
    //
    // tx_semaphore is shared with the RPC channel, so acquiring it for a
    // chunk is always attempted non-blocking (timeout 0): if RPC currently
    // holds it, coproc_tx_chunk_sent stays false and the SAME chunk is
    // retried on the next Status ack (which - since the single-slot
    // semaphore guarantees only one sender's chunk can ever be outstanding -
    // is necessarily the ack for whichever send just released it, ours or
    // RPC's; either way it's a fresh opportunity to try again). Once a chunk
    // acquire does succeed, coproc_tx_chunk_sent is set true, and the ack
    // that follows is then unambiguously ours, so the next call advances to
    // coproc_tx_chunk_next + 1 instead of retrying. coproc_run()'s own
    // timeout is the backstop if RPC traffic never leaves a gap.
    ExpansionCoprocTxRequest* coproc_tx_active;
    uint16_t coproc_tx_chunk_next;
    uint16_t coproc_tx_chunk_total;
    bool coproc_tx_chunk_sent;

    FuriHalSerialId serial_id;
    FuriHalSerialHandle* serial_handle;

    RpcSession* rpc_session;

    ExpansionCoprocCallback coproc_callback;
    void* coproc_cb_context;

    // Inbound coprocessor message reassembly state (results only; job
    // requests arriving from the module are not supported in this version
    // and are silently dropped).
    uint16_t coproc_rx_job_id;
    uint16_t coproc_rx_chunk_next;
    size_t coproc_rx_len;
    uint8_t coproc_rx_buf[EXPANSION_WORKER_COPROC_MAX_MESSAGE_SIZE];

    ExpansionWorkerState state;
    ExpansionWorkerExitReason exit_reason;
    ExpansionWorkerCallback callback;
    void* cb_context;

    // Diagnostic only: last UART RX error event bitmask, set from IRQ
    // context and logged from the worker thread (logging directly from IRQ
    // context is avoided).
    volatile FuriHalSerialRxEvent last_rx_error_event;
};

// Called in UART IRQ context
static void expansion_worker_serial_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    furi_assert(handle);
    furi_assert(context);

    ExpansionWorker* instance = context;

    if(event & (FuriHalSerialRxEventNoiseError | FuriHalSerialRxEventFrameError |
                FuriHalSerialRxEventOverrunError)) {
        instance->last_rx_error_event = event;
        furi_thread_flags_set(furi_thread_get_id(instance->thread), ExpansionWorkerFlagError);
    } else if(event & FuriHalSerialRxEventData) {
        while(furi_hal_serial_async_rx_available(handle)) {
            const uint8_t data = furi_hal_serial_async_rx(handle);
            furi_stream_buffer_send(instance->rx_buf, &data, sizeof(data), 0);
        }
        furi_thread_flags_set(furi_thread_get_id(instance->thread), ExpansionWorkerFlagData);
    }
}

typedef enum {
    ExpansionCoprocChunkSent, // Acquired tx_semaphore and sent - wait for its ack.
    ExpansionCoprocChunkBusy, // tx_semaphore currently held by RPC - retry on the next ack.
    ExpansionCoprocChunkFailed, // A real send failure - give up on this job.
} ExpansionCoprocChunkResult;

static ExpansionCoprocChunkResult
    expansion_worker_coproc_send_chunk(ExpansionWorker* instance, uint16_t chunk_seq);

// Finishes the active multi-chunk coproc send with the given result,
// releasing its caller (blocked on req->done in expansion_worker_coproc_send()).
static void expansion_worker_finish_coproc_tx(ExpansionWorker* instance, bool success) {
    ExpansionCoprocTxRequest* req = instance->coproc_tx_active;
    instance->coproc_tx_active = NULL;
    req->success = success;
    furi_semaphore_release(req->done);
}

// Attempts to (re)send instance->coproc_tx_chunk_next. On Busy, leaves state
// as-is for a retry on the next ack (see coproc_tx_chunk_sent's doc comment
// on the struct). On Failed, ends the job.
static void expansion_worker_attempt_coproc_chunk(ExpansionWorker* instance) {
    switch(expansion_worker_coproc_send_chunk(instance, instance->coproc_tx_chunk_next)) {
    case ExpansionCoprocChunkSent:
        instance->coproc_tx_chunk_sent = true;
        break;
    case ExpansionCoprocChunkBusy:
        instance->coproc_tx_chunk_sent = false;
        break;
    case ExpansionCoprocChunkFailed:
        expansion_worker_finish_coproc_tx(instance, false);
        break;
    }
}

// Drains any pending outbound coprocessor job requests and attempts the
// first chunk. Only ever called from the worker's own thread.
static void expansion_worker_service_coproc_tx(ExpansionWorker* instance) {
    if(instance->coproc_tx_active) return;

    ExpansionCoprocTxRequest* req = NULL;
    if(furi_message_queue_get(instance->coproc_tx_queue, &req, 0) != FuriStatusOk) return;

    instance->coproc_tx_active = req;
    instance->coproc_tx_chunk_next = 0;
    instance->coproc_tx_chunk_sent = false;
    instance->coproc_tx_chunk_total = req->data_size == 0 ?
                                           1 :
                                           (uint16_t)((req->data_size +
                                                        EXPANSION_PROTOCOL_COPROC_MAX_PAYLOAD_SIZE -
                                                        1) /
                                                       EXPANSION_PROTOCOL_COPROC_MAX_PAYLOAD_SIZE);

    expansion_worker_attempt_coproc_chunk(instance);
}

// Called from the Status-frame branches below once tx_semaphore has been
// released for an ack. No-op if no coproc send is active. Otherwise: if the
// ack is for our own last-sent chunk, advances (or finishes, if that was the
// last chunk); if our last attempt never actually got a chunk out (someone
// else's ack, e.g. RPC's), retries the same chunk now that a slot is free.
static void expansion_worker_advance_coproc_tx(ExpansionWorker* instance) {
    if(!instance->coproc_tx_active) return;

    if(instance->coproc_tx_chunk_sent) {
        instance->coproc_tx_chunk_next++;
        if(instance->coproc_tx_chunk_next >= instance->coproc_tx_chunk_total) {
            expansion_worker_finish_coproc_tx(instance, true);
            return;
        }
        instance->coproc_tx_chunk_sent = false;
    }

    expansion_worker_attempt_coproc_chunk(instance);
}

static size_t expansion_worker_receive_callback(uint8_t* data, size_t data_size, void* context) {
    ExpansionWorker* instance = context;

    size_t received_size = 0;

    while(true) {
        received_size += furi_stream_buffer_receive(
            instance->rx_buf, data + received_size, data_size - received_size, 0);

        if(received_size == data_size) break;

        const uint32_t flags = furi_thread_flags_wait(
            EXPANSION_ALL_FLAGS, FuriFlagWaitAny, furi_ms_to_ticks(EXPANSION_PROTOCOL_TIMEOUT_MS));

        // Serviced unconditionally, not as an `else if` branch below: Data is
        // set on every heartbeat/byte burst, so on a live link it is almost
        // always set alongside CoprocTx, and an `else if` chain would starve
        // CoprocTx of ever being handled - the caller would then block for
        // its full timeout budget on every single job. Guard on the error
        // path explicitly rather than masking out FuriFlagError: the error
        // return is a whole encoded value (e.g. FuriFlagErrorTimeout =
        // 0xFFFFFFFE), not a flag-bit union, and happens to have bit 3 set -
        // the same bit as ExpansionWorkerFlagCoprocTx - so a bitmask
        // approach would spuriously fire this on every plain timeout tick.
        if(!(flags & FuriFlagError) && (flags & ExpansionWorkerFlagCoprocTx)) {
            expansion_worker_service_coproc_tx(instance);
        }

        if(flags & FuriFlagError) {
            if(flags == (unsigned)FuriFlagErrorTimeout) {
                // Exiting due to timeout
                instance->exit_reason = ExpansionWorkerExitReasonTimeout;
            } else {
                // Exiting due to an unspecified error
                FURI_LOG_E(TAG, "furi_thread_flags_wait failed, flags=0x%08lx", flags);
                instance->exit_reason = ExpansionWorkerExitReasonError;
            }
            break;
        } else if(flags & ExpansionWorkerFlagStop) {
            // Exiting due to explicit request
            instance->exit_reason = ExpansionWorkerExitReasonUser;
            break;
        } else if(flags & ExpansionWorkerFlagError) {
            // Exiting due to a UART RX error (noise/framing/overrun) - see
            // expansion_worker_serial_rx_callback().
            FURI_LOG_E(TAG, "UART RX error, event=0x%08x", instance->last_rx_error_event);
            instance->exit_reason = ExpansionWorkerExitReasonError;
            break;
        } else if(flags & ExpansionWorkerFlagData) {
            // Go to buffer reading
            continue;
        } else if(flags & ExpansionWorkerFlagCoprocTx) {
            // Already serviced above; nothing left to do but loop back
            // and re-check the stream buffer / wait again.
            continue;
        }
    }

    return received_size;
}

static inline bool
    expansion_worker_receive_frame(ExpansionWorker* instance, ExpansionFrame* frame) {
    return expansion_protocol_decode(frame, expansion_worker_receive_callback, instance) ==
           ExpansionProtocolStatusOk;
}

static size_t
    expansion_worker_send_callback(const uint8_t* data, size_t data_size, void* context) {
    ExpansionWorker* instance = context;
    furi_hal_serial_tx(instance->serial_handle, data, data_size);
    furi_hal_serial_tx_wait_complete(instance->serial_handle);
    return data_size;
}

static inline bool
    expansion_worker_send_frame(ExpansionWorker* instance, const ExpansionFrame* frame) {
    // The RPC (via expansion_worker_rpc_send_callback, called from the RPC
    // session's own thread) and coprocessor (via expansion_worker_coproc_send,
    // called from an arbitrary caller thread) send paths both funnel through
    // here concurrently with the worker's own thread's ACK/heartbeat sends -
    // serialize actual UART writes so frames never interleave on the wire.
    furi_mutex_acquire(instance->tx_mutex, FuriWaitForever);
    const bool success = expansion_protocol_encode(
                              frame, expansion_worker_send_callback, instance) ==
                          ExpansionProtocolStatusOk;
    furi_mutex_release(instance->tx_mutex);
    return success;
}

static bool expansion_worker_send_heartbeat(ExpansionWorker* instance) {
    const ExpansionFrame frame = {
        .header.type = ExpansionFrameTypeHeartbeat,
        .content.heartbeat = {},
    };

    return expansion_worker_send_frame(instance, &frame);
}

static bool
    expansion_worker_send_status_response(ExpansionWorker* instance, ExpansionFrameError error) {
    const ExpansionFrame frame = {
        .header.type = ExpansionFrameTypeStatus,
        .content.status.error = error,
    };

    return expansion_worker_send_frame(instance, &frame);
}

static bool expansion_worker_send_data_response(
    ExpansionWorker* instance,
    const uint8_t* data,
    size_t data_size) {
    furi_assert(data_size <= EXPANSION_PROTOCOL_MAX_DATA_SIZE);

    ExpansionFrame frame = {
        .header.type = ExpansionFrameTypeData,
        .content.data.size = data_size,
    };

    memcpy(frame.content.data.bytes, data, data_size);
    return expansion_worker_send_frame(instance, &frame);
}

// Called in Rpc session thread context
static void expansion_worker_rpc_send_callback(void* context, uint8_t* data, size_t data_size) {
    ExpansionWorker* instance = context;

    for(size_t sent_data_size = 0; sent_data_size < data_size;) {
        if(furi_semaphore_acquire(
               instance->tx_semaphore, furi_ms_to_ticks(EXPANSION_PROTOCOL_TIMEOUT_MS)) !=
           FuriStatusOk) {
            furi_thread_flags_set(furi_thread_get_id(instance->thread), ExpansionWorkerFlagError);
            break;
        }

        const size_t current_data_size =
            MIN(data_size - sent_data_size, EXPANSION_PROTOCOL_MAX_DATA_SIZE);
        if(!expansion_worker_send_data_response(
               instance, data + sent_data_size, current_data_size)) {
            // Stock code used to `break` here without releasing tx_semaphore,
            // which it had already acquired above - a failed send here
            // permanently leaked one count off a binary (max-1) semaphore
            // shared with the Coproc channel, silently starving every future
            // Coproc send while RPC itself kept working (its own next
            // acquire is satisfied by the next inbound Status release, so
            // the leak was invisible from the RPC side). Release before
            // breaking so a single failed chunk send doesn't permanently
            // wedge the shared gate.
            furi_semaphore_release(instance->tx_semaphore);
            break;
        }
        sent_data_size += current_data_size;
    }
}

static bool expansion_worker_rpc_session_open(ExpansionWorker* instance) {
    Rpc* rpc = furi_record_open(RECORD_RPC);
    instance->rpc_session = rpc_session_open(rpc, RpcOwnerUart);

    if(instance->rpc_session) {
        rpc_session_set_context(instance->rpc_session, instance);
        rpc_session_set_send_bytes_callback(
            instance->rpc_session, expansion_worker_rpc_send_callback);
    }

    return instance->rpc_session != NULL;
}

static void expansion_worker_rpc_session_close(ExpansionWorker* instance) {
    if(instance->rpc_session) {
        rpc_session_close(instance->rpc_session);
    }

    furi_record_close(RECORD_RPC);
}

static bool expansion_worker_handle_state_handshake(
    ExpansionWorker* instance,
    const ExpansionFrame* rx_frame) {
    bool success = false;

    do {
        if(rx_frame->header.type != ExpansionFrameTypeBaudRate) break;
        const uint32_t baud_rate = rx_frame->content.baud_rate.baud;

        FURI_LOG_D(TAG, "Proposed baud rate: %lu", baud_rate);

        if(furi_hal_serial_is_baud_rate_supported(instance->serial_handle, baud_rate)) {
            instance->state = ExpansionWorkerStateConnected;
            instance->tx_semaphore = furi_semaphore_alloc(1, 1);
            instance->callback(instance->cb_context, ExpansionWorkerCallbackReasonConnected);
            // Send response at previous baud rate
            if(!expansion_worker_send_status_response(instance, ExpansionFrameErrorNone)) break;
            furi_hal_serial_set_br(instance->serial_handle, baud_rate);

        } else {
            if(!expansion_worker_send_status_response(instance, ExpansionFrameErrorBaudRate))
                break;
            FURI_LOG_E(TAG, "Bad baud rate");
        }
        success = true;
    } while(false);

    return success;
}

// Reassembles inbound coprocessor result chunks and, once a message is
// complete, invokes the registered callback. Always ACKs (Status) the frame;
// out-of-sequence or oversized messages are dropped and NAK'd rather than
// killing the connection. Request-flagged frames (module -> Flipper job
// submission) are not supported in this version and are silently dropped.
static bool
    expansion_worker_handle_coproc_frame(ExpansionWorker* instance, const ExpansionFrame* rx_frame) {
    const ExpansionFrameCoproc* coproc = &rx_frame->content.coproc;
    ExpansionFrameError ack_error = ExpansionFrameErrorNone;

    do {
        if(coproc->flags & ExpansionFrameCoprocFlagRequest) break;

        if(coproc->flags & ExpansionFrameCoprocFlagFirst) {
            instance->coproc_rx_job_id = coproc->job_id;
            instance->coproc_rx_chunk_next = 0;
            instance->coproc_rx_len = 0;
        }

        if(coproc->job_id != instance->coproc_rx_job_id ||
           coproc->chunk_seq != instance->coproc_rx_chunk_next ||
           coproc->payload_len > sizeof(instance->coproc_rx_buf) - instance->coproc_rx_len) {
            instance->coproc_rx_chunk_next = 0;
            instance->coproc_rx_len = 0;
            ack_error = ExpansionFrameErrorUnknown;
            break;
        }

        memcpy(
            instance->coproc_rx_buf + instance->coproc_rx_len,
            coproc->payload,
            coproc->payload_len);
        instance->coproc_rx_len += coproc->payload_len;
        instance->coproc_rx_chunk_next++;

        if(coproc->flags & ExpansionFrameCoprocFlagLast) {
            if(instance->coproc_callback) {
                instance->coproc_callback(
                    instance->coproc_cb_context,
                    coproc->job_id,
                    (coproc->flags & ExpansionFrameCoprocFlagError) != 0,
                    instance->coproc_rx_buf,
                    instance->coproc_rx_len);
            }
            instance->coproc_rx_chunk_next = 0;
            instance->coproc_rx_len = 0;
        }
    } while(false);

    return expansion_worker_send_status_response(instance, ack_error);
}

static bool expansion_worker_handle_state_connected(
    ExpansionWorker* instance,
    const ExpansionFrame* rx_frame) {
    bool success = false;

    do {
        if(rx_frame->header.type == ExpansionFrameTypeControl) {
            const uint8_t command = rx_frame->content.control.command;
            if(command == ExpansionFrameControlCommandStartRpc) {
                if(!expansion_worker_rpc_session_open(instance)) break;
                instance->state = ExpansionWorkerStateRpcActive;
            } else if(command == ExpansionFrameControlCommandEnableOtg) {
                Power* power = furi_record_open(RECORD_POWER);
                power_enable_otg(power, true);
                furi_record_close(RECORD_POWER);
            } else if(command == ExpansionFrameControlCommandDisableOtg) {
                Power* power = furi_record_open(RECORD_POWER);
                power_enable_otg(power, false);
                furi_record_close(RECORD_POWER);
            } else {
                break;
            }

            if(!expansion_worker_send_status_response(instance, ExpansionFrameErrorNone)) break;

        } else if(rx_frame->header.type == ExpansionFrameTypeHeartbeat) {
            if(!expansion_worker_send_heartbeat(instance)) break;

        } else if(rx_frame->header.type == ExpansionFrameTypeCoproc) {
            if(!expansion_worker_handle_coproc_frame(instance, rx_frame)) break;

        } else if(rx_frame->header.type == ExpansionFrameTypeStatus) {
            if(rx_frame->content.status.error != ExpansionFrameErrorNone) break;
            furi_semaphore_release(instance->tx_semaphore);
            expansion_worker_advance_coproc_tx(instance);

        } else {
            break;
        }
        success = true;
    } while(false);

    return success;
}

static bool expansion_worker_handle_state_rpc_active(
    ExpansionWorker* instance,
    const ExpansionFrame* rx_frame) {
    bool success = false;

    do {
        if(rx_frame->header.type == ExpansionFrameTypeData) {
            if(!expansion_worker_send_status_response(instance, ExpansionFrameErrorNone)) break;

            const size_t size_consumed = rpc_session_feed(
                instance->rpc_session,
                rx_frame->content.data.bytes,
                rx_frame->content.data.size,
                EXPANSION_PROTOCOL_TIMEOUT_MS);
            if(size_consumed != rx_frame->content.data.size) {
                FURI_LOG_E(
                    TAG,
                    "rpc_session_feed short: consumed %zu of %u",
                    size_consumed,
                    rx_frame->content.data.size);
                break;
            }

        } else if(rx_frame->header.type == ExpansionFrameTypeControl) {
            const uint8_t command = rx_frame->content.control.command;
            if(command == ExpansionFrameControlCommandStopRpc) {
                instance->state = ExpansionWorkerStateConnected;
                expansion_worker_rpc_session_close(instance);
            } else {
                break;
            }

            if(!expansion_worker_send_status_response(instance, ExpansionFrameErrorNone)) break;

        } else if(rx_frame->header.type == ExpansionFrameTypeStatus) {
            if(rx_frame->content.status.error != ExpansionFrameErrorNone) break;
            furi_semaphore_release(instance->tx_semaphore);
            expansion_worker_advance_coproc_tx(instance);

        } else if(rx_frame->header.type == ExpansionFrameTypeHeartbeat) {
            if(!expansion_worker_send_heartbeat(instance)) break;

        } else if(rx_frame->header.type == ExpansionFrameTypeCoproc) {
            if(!expansion_worker_handle_coproc_frame(instance, rx_frame)) break;

        } else {
            break;
        }
        success = true;
    } while(false);

    return success;
}

typedef bool (*ExpansionWorkerStateHandler)(ExpansionWorker*, const ExpansionFrame*);

static const ExpansionWorkerStateHandler expansion_handlers[] = {
    [ExpansionWorkerStateHandShake] = expansion_worker_handle_state_handshake,
    [ExpansionWorkerStateConnected] = expansion_worker_handle_state_connected,
    [ExpansionWorkerStateRpcActive] = expansion_worker_handle_state_rpc_active,
};

static inline void expansion_worker_state_machine(ExpansionWorker* instance) {
    ExpansionFrame rx_frame;

    while(true) {
        if(!expansion_worker_receive_frame(instance, &rx_frame)) {
            // Fires constantly and harmlessly during normal detect-pin retry
            // cycling (e.g. decoding the VGM's single-byte announce pulse as
            // an incomplete/invalid frame) - not logged to avoid competing
            // for CPU time with the UART ISR while investigating overruns.
            // The UART RX error log below (inside the error branch that sets
            // exit_reason) is the one that matters.
            break;
        }
        if(!expansion_handlers[instance->state](instance, &rx_frame)) {
            FURI_LOG_E(
                TAG,
                "handler failed, state=%d frame_type=%u",
                instance->state,
                rx_frame.header.type);
            break;
        }
    }
}

static int32_t expansion_worker(void* context) {
    furi_assert(context);
    ExpansionWorker* instance = context;

    furi_hal_power_insomnia_enter();

    instance->serial_handle = furi_hal_serial_control_acquire(instance->serial_id);
    furi_check(instance->serial_handle);

    FURI_LOG_D(TAG, "Worker started");

    instance->state = ExpansionWorkerStateHandShake;
    instance->exit_reason = ExpansionWorkerExitReasonUnknown;

    furi_hal_serial_init(instance->serial_handle, EXPANSION_PROTOCOL_DEFAULT_BAUD_RATE);

    furi_hal_serial_async_rx_start(
        instance->serial_handle, expansion_worker_serial_rx_callback, instance, true);

    if(expansion_worker_send_heartbeat(instance)) {
        expansion_worker_state_machine(instance);
    }

    furi_hal_serial_async_rx_stop(instance->serial_handle);

    if(instance->state == ExpansionWorkerStateRpcActive) {
        expansion_worker_rpc_session_close(instance);
    }

    if(instance->tx_semaphore) {
        furi_semaphore_free(instance->tx_semaphore);
        instance->tx_semaphore = NULL;
    }

    FURI_LOG_D(TAG, "Worker stopped");

    furi_hal_serial_control_release(instance->serial_handle);
    furi_hal_power_insomnia_exit();

    // Do not invoke worker callback on user-requested exit
    if((instance->exit_reason != ExpansionWorkerExitReasonUser) && (instance->callback != NULL)) {
        instance->callback(instance->cb_context, ExpansionWorkerCallbackReasonExit);
    }

    return 0;
}

ExpansionWorker* expansion_worker_alloc(FuriHalSerialId serial_id) {
    ExpansionWorker* instance = malloc(sizeof(ExpansionWorker));

    instance->thread = furi_thread_alloc_ex(
        TAG "Worker", EXPANSION_WORKER_STACK_SZIE, expansion_worker, instance);
    instance->rx_buf = furi_stream_buffer_alloc(EXPANSION_WORKER_BUFFER_SIZE, 1);
    instance->tx_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    instance->coproc_tx_queue = furi_message_queue_alloc(1, sizeof(ExpansionCoprocTxRequest*));
    instance->tx_semaphore = NULL;
    instance->coproc_tx_active = NULL;
    instance->coproc_tx_chunk_next = 0;
    instance->coproc_tx_chunk_total = 0;
    instance->coproc_tx_chunk_sent = false;
    instance->serial_id = serial_id;

    instance->coproc_callback = NULL;
    instance->coproc_cb_context = NULL;
    instance->coproc_rx_job_id = 0;
    instance->coproc_rx_chunk_next = 0;
    instance->coproc_rx_len = 0;
    instance->last_rx_error_event = 0;

    // Improves responsiveness in heavy games at the expense of dropped frames
    furi_thread_set_priority(instance->thread, FuriThreadPriorityLow);

    return instance;
}

void expansion_worker_free(ExpansionWorker* instance) {
    furi_stream_buffer_free(instance->rx_buf);
    furi_thread_join(instance->thread);
    furi_thread_free(instance->thread);
    furi_mutex_free(instance->tx_mutex);
    furi_message_queue_free(instance->coproc_tx_queue);
    free(instance);
}

void expansion_worker_set_callback(
    ExpansionWorker* instance,
    ExpansionWorkerCallback callback,
    void* context) {
    instance->callback = callback;
    instance->cb_context = context;
}

void expansion_worker_set_coproc_callback(
    ExpansionWorker* instance,
    ExpansionCoprocCallback callback,
    void* context) {
    instance->coproc_callback = callback;
    instance->coproc_cb_context = context;
}

void expansion_worker_start(ExpansionWorker* instance) {
    furi_thread_start(instance->thread);
}

void expansion_worker_stop(ExpansionWorker* instance) {
    furi_thread_flags_set(furi_thread_get_id(instance->thread), ExpansionWorkerFlagStop);
    furi_thread_join(instance->thread);
}

// Sends a single chunk of the active coproc send (instance->coproc_tx_active).
// Called either to kick off chunk 0, or from a Status-ack handler to send the
// next chunk - never in a loop that blocks waiting for the ack of the chunk
// it just sent, since that ack can only ever be processed by this same
// thread returning to its normal receive loop (see the struct comment on
// coproc_tx_active for the self-deadlock this replaced).
static ExpansionCoprocChunkResult
    expansion_worker_coproc_send_chunk(ExpansionWorker* instance, uint16_t chunk_seq) {
    const ExpansionCoprocTxRequest* req = instance->coproc_tx_active;
    const uint16_t chunk_total = instance->coproc_tx_chunk_total;
    const size_t offset = (size_t)chunk_seq * EXPANSION_PROTOCOL_COPROC_MAX_PAYLOAD_SIZE;
    const size_t chunk_len =
        MIN(req->data_size - offset, (size_t)EXPANSION_PROTOCOL_COPROC_MAX_PAYLOAD_SIZE);

    uint8_t flags = ExpansionFrameCoprocFlagRequest;
    if(chunk_seq == 0) flags |= ExpansionFrameCoprocFlagFirst;
    if(chunk_seq == chunk_total - 1) flags |= ExpansionFrameCoprocFlagLast;

    ExpansionFrame frame = {
        .header.type = ExpansionFrameTypeCoproc,
        .content.coproc =
            {
                .job_id = req->job_id,
                .task_type = req->task_type,
                .flags = flags,
                .chunk_seq = chunk_seq,
                .chunk_total = chunk_total,
                .payload_len = chunk_len,
            },
    };
    memcpy(frame.content.coproc.payload, req->data + offset, chunk_len);

    // Non-blocking: if RPC currently holds tx_semaphore, don't wait for it
    // here (this thread is the only one that can ever process the ack that
    // would release it) - report Busy so the caller retries on the next ack
    // instead.
    if(furi_semaphore_acquire(instance->tx_semaphore, 0) != FuriStatusOk) {
        return ExpansionCoprocChunkBusy;
    }

    if(!expansion_worker_send_frame(instance, &frame)) {
        furi_semaphore_release(instance->tx_semaphore);
        return ExpansionCoprocChunkFailed;
    }

    return ExpansionCoprocChunkSent;
}

bool expansion_worker_coproc_send(
    ExpansionWorker* instance,
    uint16_t job_id,
    uint8_t task_type,
    const uint8_t* data,
    size_t data_size) {
    // Never touch tx_semaphore or the UART handle from this (caller's)
    // thread - hand the request to the worker thread and block on a
    // dedicated, bounded-wait completion semaphore instead. `data` is only
    // read by the worker thread before this function returns, so it's safe
    // for it to point at the caller's stack/heap buffer for the duration of
    // this call.
    ExpansionCoprocTxRequest req = {
        .job_id = job_id,
        .task_type = task_type,
        .data = data,
        .data_size = data_size,
        .success = false,
        .done = furi_semaphore_alloc(1, 0),
    };
    ExpansionCoprocTxRequest* req_ptr = &req;

    bool success = false;

    const FuriStatus put_status = furi_message_queue_put(
        instance->coproc_tx_queue, &req_ptr, furi_ms_to_ticks(EXPANSION_PROTOCOL_TIMEOUT_MS));

    if(put_status == FuriStatusOk) {
        furi_thread_flags_set(furi_thread_get_id(instance->thread), ExpansionWorkerFlagCoprocTx);

        // Sized to THIS request's chunk count (not the worst-case max
        // message size) so a small job isn't stuck with a multi-second
        // budget - a timeout here should only happen if the worker thread is
        // wedged elsewhere, a condition this whole redesign exists to
        // eliminate.
        const uint32_t chunk_total = data_size == 0 ?
                                          1 :
                                          (uint32_t)((data_size +
                                                       EXPANSION_PROTOCOL_COPROC_MAX_PAYLOAD_SIZE - 1) /
                                                      EXPANSION_PROTOCOL_COPROC_MAX_PAYLOAD_SIZE);
        const uint32_t wait_ms = EXPANSION_PROTOCOL_TIMEOUT_MS * (chunk_total + 2);
        const FuriStatus done_status = furi_semaphore_acquire(req.done, furi_ms_to_ticks(wait_ms));
        if(done_status == FuriStatusOk) {
            success = req.success;
        }
    }

    furi_semaphore_free(req.done);
    return success;
}
