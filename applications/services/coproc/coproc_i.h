#pragma once

#include "coproc.h"

#include <furi.h>
#include <toolbox/api_lock.h>
#include <expansion/expansion.h>

struct Coproc {
    FuriThread* thread;

    Expansion* expansion;

    CoprocFallbackFn fallbacks[CoprocTaskCount];
    void* fallback_contexts[CoprocTaskCount];

    // Serializes coproc_run() callers against each other and against the
    // link's actual one-job-at-a-time capacity. Held for the whole duration
    // of a coproc_run() call (offload attempt + fallback), but NOT held
    // across the wait for coproc_expansion_callback() - see pending_mutex.
    FuriMutex* job_mutex;
    uint16_t next_job_id; // only touched while job_mutex is held

    // Guards the fields below, which coproc_expansion_callback() (running on
    // an expansion-service thread) and coproc_run() (running on an arbitrary
    // caller thread) both touch. Never held across a blocking wait, on
    // either side, to avoid a submit-then-wait / deliver deadlock.
    FuriMutex* pending_mutex;
    uint16_t pending_job_id; // 0 == no job currently pending
    uint8_t* pending_output; // caller's buffer; NULL once retired
    size_t pending_output_capacity;
    size_t pending_result_size;
    bool pending_result_success;
    bool pending_delivered;
    FuriApiLock pending_lock;
};

// Called on an expansion-service thread when a job result is fully
// reassembled. Matches against pending_job_id; a result for a job that
// coproc_run() has already abandoned (timeout) is silently dropped - see the
// comment in coproc_run() for why the corresponding FuriApiLock is leaked,
// not freed, in that case.
void coproc_expansion_callback(
    void* context,
    uint16_t job_id,
    bool is_error,
    const uint8_t* data,
    size_t data_size);
