#include "lfrfid_write_targets.h"

#include <furi.h>
#include <string.h>

// Strictly less than: LFRFID_WRITE_TARGET_MASK_ALL shifts by LFRFIDWriteTargetMax, and a shift
// by the full width of the type is undefined.
_Static_assert(
    LFRFIDWriteTargetMax < 32,
    "A write target mask is a uint32_t, so there is room for 31 targets");

// Saved masks store these as bit positions, so appending is free but reordering silently
// reinterprets every existing settings file - see lfrfid_settings.c.
_Static_assert(
    LFRFIDWriteTargetT5577 == 0 && LFRFIDWriteTargetEM4305 == 1,
    "Saved masks pin these bit positions - append new targets, never reorder");

static const char* const lfrfid_write_target_names[LFRFIDWriteTargetMax] = {
    [LFRFIDWriteTargetT5577] = "T5577",
    [LFRFIDWriteTargetEM4305] = "EM4305",
};

// No default: appending a target fails the build until someone decides this, rather than
// silently shipping it enabled. Answering "on" also means bumping LFRFID_SETTINGS_VERSION,
// or existing files read the new bit as 0 and only fresh installs get it.
static bool lfrfid_write_target_is_default(LFRFIDWriteTarget target) {
    switch(target) {
    case LFRFIDWriteTargetT5577:
    case LFRFIDWriteTargetEM4305:
        // Both were always tried, so both stay on by default.
        return true;
    case LFRFIDWriteTargetMax:
        break;
    }

    furi_crash("Unknown write target");
}

LFRFIDWriteTargetMask lfrfid_write_targets_default(void) {
    LFRFIDWriteTargetMask mask = 0;

    for(LFRFIDWriteTarget target = 0; target < LFRFIDWriteTargetMax; target++) {
        if(lfrfid_write_target_is_default(target)) mask |= LFRFID_WRITE_TARGET_BIT(target);
    }

    return mask;
}

LFRFIDWriteType lfrfid_write_target_type(LFRFIDWriteTarget target) {
    // No default: -Wswitch is an error here, so a target appended to the enum fails the build
    // until it is classified, rather than silently inheriting another chip's encoding.
    switch(target) {
    case LFRFIDWriteTargetT5577:
        return LFRFIDWriteTypeT5577;
    case LFRFIDWriteTargetEM4305:
        return LFRFIDWriteTypeEM4305;
    case LFRFIDWriteTargetMax:
        break;
    }

    furi_crash("Unknown write target");
}

const char* lfrfid_write_target_name(LFRFIDWriteTarget target) {
    furi_check(target < LFRFIDWriteTargetMax);

    const char* name = lfrfid_write_target_names[target];
    furi_check(name); // catches a target appended without a row above

    return name;
}

LFRFIDWriteTargetMask lfrfid_write_targets_supported(ProtocolDict* dict, ProtocolId protocol) {
    furi_check(dict);
    furi_check(protocol != PROTOCOL_NO);

    // Heap, not stack: LFRFIDWriteRequest is sizeable and callers may be on a 2K stack.
    LFRFIDWriteRequest* request = malloc(sizeof(LFRFIDWriteRequest));
    LFRFIDWriteTargetMask mask = 0;

    for(LFRFIDWriteTarget target = 0; target < LFRFIDWriteTargetMax; target++) {
        memset(request, 0, sizeof(LFRFIDWriteRequest));
        request->write_type = lfrfid_write_target_type(target);

        if(protocol_dict_get_write_data(dict, protocol, request)) {
            mask |= LFRFID_WRITE_TARGET_BIT(target);
        }
    }

    free(request);

    return mask;
}
