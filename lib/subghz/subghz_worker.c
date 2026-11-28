#include "subghz_worker.h"

#include <furi.h>

#define TAG "SubGhzWorker"

struct SubGhzWorker {
    FuriThread* thread;
    FuriStreamBuffer* stream;

    volatile bool running;
    volatile bool overrun;

    LevelDuration filter_level_duration;
    uint16_t filter_duration;

    SubGhzWorkerOverrunCallback overrun_callback;
    SubGhzWorkerPairCallback pair_callback;
    void* context;
};

/** Rx callback timer
 * 
 * @param level received signal level
 * @param duration received signal duration
 * @param context 
 */
void subghz_worker_rx_callback(bool level, uint32_t duration, void* context) {
    SubGhzWorker* instance = context;
    // The buffer only exists once the worker has been started; nothing reads
    // samples before that, so drop them.
    if(!instance->stream) return;

    LevelDuration level_duration = level_duration_make(level, duration);
    if(instance->overrun) {
        instance->overrun = false;
        level_duration = level_duration_reset();
    }
    size_t ret =
        furi_stream_buffer_send(instance->stream, &level_duration, sizeof(LevelDuration), 0);
    if(sizeof(LevelDuration) != ret) instance->overrun = true;
}

/** Worker callback thread
 * 
 * @param context 
 * @return exit code 
 */
static int32_t subghz_worker_thread_callback(void* context) {
    SubGhzWorker* instance = context;

    LevelDuration level_duration;
    while(instance->running) {
        int ret = furi_stream_buffer_receive(
            instance->stream, &level_duration, sizeof(LevelDuration), 10);
        if(ret == sizeof(LevelDuration)) {
            if(level_duration_is_reset(level_duration)) {
                FURI_LOG_E(TAG, "Overrun buffer");
                if(instance->overrun_callback) instance->overrun_callback(instance->context);
            } else {
                bool level = level_duration_get_level(level_duration);
                uint32_t duration = level_duration_get_duration(level_duration);

                if((duration < instance->filter_duration) ||
                   (instance->filter_level_duration.level == level)) {
                    instance->filter_level_duration.duration += duration;

                } else if(instance->filter_level_duration.level != level) {
                    if(instance->pair_callback)
                        instance->pair_callback(
                            instance->context,
                            instance->filter_level_duration.level,
                            instance->filter_level_duration.duration);

                    instance->filter_level_duration.duration = duration;
                    instance->filter_level_duration.level = level;
                }
            }
        }
    }

    return 0;
}

SubGhzWorker* subghz_worker_alloc(void) {
    SubGhzWorker* instance = malloc(sizeof(SubGhzWorker));

    // The thread (2 KB stack) and the 16 KB sample buffer are allocated on the
    // first subghz_worker_start(): apps allocate a worker up front, but many
    // sessions never receive, and that RAM is what other services need while
    // the app is open. Once allocated they stay until subghz_worker_free(), so
    // a later start never has to find 18 KB again.

    //setting default filter in us
    instance->filter_duration = 30;

    return instance;
}

void subghz_worker_free(SubGhzWorker* instance) {
    furi_check(instance);

    if(instance->stream) {
        // Make the rx callback see "no buffer" before it goes away
        FURI_CRITICAL_ENTER();
        FuriStreamBuffer* stream = instance->stream;
        instance->stream = NULL;
        FURI_CRITICAL_EXIT();
        furi_stream_buffer_free(stream);
    }
    if(instance->thread) furi_thread_free(instance->thread);

    free(instance);
}

void subghz_worker_set_overrun_callback(
    SubGhzWorker* instance,
    SubGhzWorkerOverrunCallback callback) {
    furi_check(instance);
    instance->overrun_callback = callback;
}

void subghz_worker_set_pair_callback(SubGhzWorker* instance, SubGhzWorkerPairCallback callback) {
    furi_check(instance);
    instance->pair_callback = callback;
}

void subghz_worker_set_context(SubGhzWorker* instance, void* context) {
    furi_check(instance);
    instance->context = context;
}

void subghz_worker_start(SubGhzWorker* instance) {
    furi_check(instance);
    furi_check(!instance->running);

    if(!instance->thread) {
        instance->thread =
            furi_thread_alloc_ex("SubGhzWorker", 2048, subghz_worker_thread_callback, instance);
    }
    if(!instance->stream) {
        instance->stream =
            furi_stream_buffer_alloc(sizeof(LevelDuration) * 4096, sizeof(LevelDuration));
    }

    instance->running = true;

    furi_thread_start(instance->thread);
}

void subghz_worker_stop(SubGhzWorker* instance) {
    furi_check(instance);
    furi_check(instance->running);

    instance->running = false;

    furi_thread_join(instance->thread);
}

bool subghz_worker_is_running(SubGhzWorker* instance) {
    furi_check(instance);
    return instance->running;
}

void subghz_worker_set_filter(SubGhzWorker* instance, uint16_t timeout) {
    furi_check(instance);
    instance->filter_duration = timeout;
}
