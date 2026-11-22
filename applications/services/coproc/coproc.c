#include "coproc_i.h"

#include <lib/toolbox/crc32_calc.h>
#include <lib/toolbox/compress.h>

#define TAG "Coproc"

#define COPROC_QUEUE_SIZE (1UL)
#define COPROC_STACK_SIZE (768UL)

// Default local fallbacks, wired to Momentum's existing crc32/compress
// utilities so behavior is unchanged when no module is connected. See
// coproc.h for why CoprocTaskCrc32 in particular is a pipeline smoke test,
// not a speed win.

static bool coproc_fallback_crc32(
    const uint8_t* input,
    size_t input_size,
    uint8_t* output,
    size_t* output_size,
    void* context) {
    UNUSED(context);
    if(*output_size < sizeof(uint32_t)) return false;

    const uint32_t crc = crc32_calc_buffer(0, input, input_size);
    memcpy(output, &crc, sizeof(crc));
    *output_size = sizeof(crc);
    return true;
}

static bool coproc_fallback_compress(
    const uint8_t* input,
    size_t input_size,
    uint8_t* output,
    size_t* output_size,
    void* context) {
    UNUSED(context);

    Compress* compress =
        compress_alloc(CompressTypeHeatshrink, &compress_config_heatshrink_default);
    size_t result_size = 0;
    const bool success = compress_encode(
        compress, (uint8_t*)input, input_size, output, *output_size, &result_size);
    compress_free(compress);

    if(success) *output_size = result_size;
    return success;
}

static bool coproc_fallback_decompress(
    const uint8_t* input,
    size_t input_size,
    uint8_t* output,
    size_t* output_size,
    void* context) {
    UNUSED(context);

    Compress* compress =
        compress_alloc(CompressTypeHeatshrink, &compress_config_heatshrink_default);
    size_t result_size = 0;
    const bool success = compress_decode(
        compress, (uint8_t*)input, input_size, output, *output_size, &result_size);
    compress_free(compress);

    if(success) *output_size = result_size;
    return success;
}

void coproc_expansion_callback(
    void* context,
    uint16_t job_id,
    bool is_error,
    const uint8_t* data,
    size_t data_size) {
    Coproc* instance = context;

    furi_mutex_acquire(instance->pending_mutex, FuriWaitForever);

    if(instance->pending_job_id == job_id) {
        // A result bigger than the caller's buffer would otherwise be cut
        // short and reported as success; treat it as a failure instead so
        // coproc_run() falls back to the local implementation.
        if(data_size > instance->pending_output_capacity) {
            FURI_LOG_W(
                TAG,
                "Result %zu B > buffer %zu B, falling back",
                data_size,
                instance->pending_output_capacity);
            is_error = true;
        }
        const size_t copy_size = is_error ? 0 : data_size;
        if(!is_error && instance->pending_output) {
            memcpy(instance->pending_output, data, copy_size);
        }
        instance->pending_result_size = copy_size;
        instance->pending_result_success = !is_error;
        instance->pending_delivered = true;

        FuriApiLock lock = instance->pending_lock;
        furi_mutex_release(instance->pending_mutex);
        // Wakes coproc_run(); ownership of freeing `lock` belongs to
        // coproc_run() from here on, not to this callback - see coproc_run().
        api_lock_unlock(lock);
        return;
    }

    // Stale result for a job coproc_run() already gave up waiting on - drop it.
    furi_mutex_release(instance->pending_mutex);
}

int32_t coproc_srv(void* p) {
    UNUSED(p);
    Coproc* instance = malloc(sizeof(Coproc));

    instance->queue = furi_message_queue_alloc(COPROC_QUEUE_SIZE, sizeof(uint8_t));
    instance->job_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    instance->next_job_id = 0;
    instance->pending_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    instance->pending_job_id = 0;
    instance->pending_output = NULL;
    instance->pending_lock = NULL;

    for(size_t i = 0; i < CoprocTaskCount; i++) {
        instance->fallbacks[i] = NULL;
        instance->fallback_contexts[i] = NULL;
    }

    instance->expansion = furi_record_open(RECORD_EXPANSION);
    expansion_set_coproc_callback(instance->expansion, coproc_expansion_callback, instance);

    coproc_register_fallback(instance, CoprocTaskCrc32, coproc_fallback_crc32, NULL);
    coproc_register_fallback(
        instance, CoprocTaskCompressHeatshrink, coproc_fallback_compress, NULL);
    coproc_register_fallback(
        instance, CoprocTaskDecompressHeatshrink, coproc_fallback_decompress, NULL);

    furi_record_create(RECORD_COPROC, instance);

    // Idle forever - this service has no commands of its own; coproc_run()
    // and friends operate directly on the shared Coproc struct via mutexes
    // from the calling thread, same as the callback above.
    uint8_t unused;
    for(;;) {
        furi_message_queue_get(instance->queue, &unused, FuriWaitForever);
    }

    return 0;
}

bool coproc_is_available(Coproc* instance) {
    furi_check(instance);
    return expansion_is_connected(instance->expansion);
}

bool coproc_supports(Coproc* instance, CoprocTaskType task) {
    furi_check(instance);
    return task < CoprocTaskCount && coproc_is_available(instance);
}

void coproc_register_fallback(
    Coproc* instance,
    CoprocTaskType task,
    CoprocFallbackFn fn,
    void* context) {
    furi_check(instance);
    furi_check(task < CoprocTaskCount);

    instance->fallbacks[task] = fn;
    instance->fallback_contexts[task] = context;
}

bool coproc_run(
    Coproc* instance,
    CoprocTaskType task,
    const uint8_t* input,
    size_t input_size,
    uint8_t* output,
    size_t* output_size,
    uint32_t timeout_ms,
    bool* was_offloaded) {
    furi_check(instance);
    furi_check(output_size);
    furi_check(task < CoprocTaskCount);

    if(was_offloaded) *was_offloaded = false;

    furi_mutex_acquire(instance->job_mutex, FuriWaitForever);

    bool offloaded = false;
    bool delivered = false;
    FuriApiLock lock = NULL;
    const uint32_t start_tick = furi_get_tick();

    if(expansion_is_connected(instance->expansion)) {
        // job_mutex-serialized, so a plain increment is safe; skip 0, which
        // means "no job pending" in pending_job_id.
        do {
            instance->next_job_id++;
        } while(instance->next_job_id == 0);
        const uint16_t job_id = instance->next_job_id;

        lock = api_lock_alloc_locked();

        furi_mutex_acquire(instance->pending_mutex, FuriWaitForever);
        instance->pending_job_id = job_id;
        instance->pending_output = output;
        instance->pending_output_capacity = *output_size;
        instance->pending_result_size = 0;
        instance->pending_result_success = false;
        instance->pending_delivered = false;
        instance->pending_lock = lock;
        furi_mutex_release(instance->pending_mutex);

        const bool sent =
            expansion_coproc_send(instance->expansion, job_id, (uint8_t)task, input, input_size);

        if(sent) {
            furi_event_flag_wait(
                lock, API_LOCK_EVENT, FuriFlagWaitAny, furi_ms_to_ticks(timeout_ms));
        }

        furi_mutex_acquire(instance->pending_mutex, FuriWaitForever);
        delivered = instance->pending_delivered;
        if(delivered) {
            *output_size = instance->pending_result_size;
            offloaded = instance->pending_result_success;
        }
        if(instance->pending_job_id == job_id) {
            // Retire: a late callback for this job_id will no longer match
            // and will leave `output`/`lock` alone - see coproc_expansion_callback().
            instance->pending_job_id = 0;
            instance->pending_output = NULL;
            instance->pending_lock = NULL;
        }
        furi_mutex_release(instance->pending_mutex);
    }

    furi_mutex_release(instance->job_mutex);

    if(lock) {
        if(delivered) {
            api_lock_free(lock);
        }
        // else: intentionally leaked - a result may still arrive and
        // reference it from coproc_expansion_callback(); freeing here would
        // race a concurrent api_lock_unlock() on freed memory. Acceptable
        // for occasional jobs; see the struct comment in coproc_i.h.
    }

    if(lock) {
        // Round-trip timing for the link (send, module compute, result
        // back). Debug level so it costs nothing unless the log level is
        // raised; use it to compare baud rates / RX paths on hardware.
        FURI_LOG_D(
            TAG,
            "task %u: %zu B in, %s after %lu ms",
            (unsigned)task,
            input_size,
            offloaded ? "offloaded" : (delivered ? "module failed" : "timed out"),
            (unsigned long)(furi_get_tick() - start_tick)); // 1 tick = 1 ms
    }

    if(offloaded) {
        if(was_offloaded) *was_offloaded = true;
        return true;
    }

    CoprocFallbackFn fallback = instance->fallbacks[task];
    if(!fallback) return false;

    return fallback(input, input_size, output, output_size, instance->fallback_contexts[task]);
}
