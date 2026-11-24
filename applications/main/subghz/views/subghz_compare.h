#pragma once

#include <gui/view.h>
#include "../helpers/subghz_compare.h"

typedef struct SubGhzViewCompare SubGhzViewCompare;

SubGhzViewCompare* subghz_view_compare_alloc(void);

void subghz_view_compare_free(SubGhzViewCompare* instance);

View* subghz_view_compare_get_view(SubGhzViewCompare* instance);

/** Show the comparison of two loaded captures. Data is copied. */
void subghz_view_compare_set_result(
    SubGhzViewCompare* instance,
    const SubGhzCompareCapture* a,
    const SubGhzCompareCapture* b,
    const SubGhzCompareResult* result,
    const char* name_a,
    const char* name_b);

/** Show a message instead of a comparison (e.g. a RAW file was picked). */
void subghz_view_compare_set_error(SubGhzViewCompare* instance, const char* text);
