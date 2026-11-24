#include "subghz_compare.h"

#include <furi.h>
#include <gui/elements.h>
#include <input/input.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define SUMMARY_LINES_MAX     12
#define SUMMARY_LINE_LEN      36
#define SUMMARY_LINES_VISIBLE 5
#define CELLS_PER_ROW         16
#define ROWS_VISIBLE          2

typedef enum {
    SubGhzCompareViewPageSummary,
    SubGhzCompareViewPageBits,
} SubGhzCompareViewPage;

struct SubGhzViewCompare {
    View* view;
};

typedef struct {
    bool has_result;
    SubGhzCompareCapture a;
    SubGhzCompareCapture b;
    SubGhzCompareResult result;
    char summary[SUMMARY_LINES_MAX][SUMMARY_LINE_LEN];
    uint8_t summary_count;
    uint8_t summary_offset;
    SubGhzCompareViewPage page;
    bool hex;
    uint16_t row_offset;
} SubGhzViewCompareModel;

static uint16_t subghz_view_compare_total_rows(const SubGhzViewCompareModel* model) {
    uint16_t cells_a, cells_b;
    if(model->hex) {
        cells_a = subghz_compare_nibble_count(&model->a);
        cells_b = subghz_compare_nibble_count(&model->b);
    } else {
        cells_a = model->a.bit_count;
        cells_b = model->b.bit_count;
    }
    uint16_t cells = MAX(cells_a, cells_b);
    return (cells + CELLS_PER_ROW - 1) / CELLS_PER_ROW;
}

static void
    subghz_view_compare_draw_cell(Canvas* canvas, int32_t x, int32_t y, char c, bool mark) {
    char str[2] = {c, 0};
    if(mark) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, x - 1, y - 8, 7, 10);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, x, y, str);
        canvas_set_color(canvas, ColorBlack);
    } else {
        canvas_draw_str(canvas, x, y, str);
    }
}

static void subghz_view_compare_draw_row(
    Canvas* canvas,
    const SubGhzViewCompareModel* model,
    const SubGhzCompareCapture* capture,
    uint16_t row,
    int32_t y,
    const char* label) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, y, label);
    canvas_set_font(canvas, FontKeyboard);

    uint8_t pad = subghz_compare_hex_pad(capture);
    uint16_t limit = model->hex ? subghz_compare_nibble_count(capture) : capture->bit_count;

    for(uint8_t i = 0; i < CELLS_PER_ROW; i++) {
        uint16_t index = row * CELLS_PER_ROW + i;
        if(index >= limit) break;
        int32_t x = 9 + i * 7 + (i >= CELLS_PER_ROW / 2 ? 3 : 0);
        char c;
        bool mark;
        if(model->hex) {
            c = subghz_compare_nibble_char(capture, index);
            mark = subghz_compare_nibble_differs(&model->result, pad, index);
        } else {
            c = subghz_compare_capture_get_bit(capture, index) ? '1' : '0';
            mark = subghz_compare_result_bit_differs(&model->result, index);
        }
        subghz_view_compare_draw_cell(canvas, x, y, c, mark);
    }
}

static void subghz_view_compare_draw_summary(Canvas* canvas, SubGhzViewCompareModel* model) {
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < SUMMARY_LINES_VISIBLE; i++) {
        uint8_t line = model->summary_offset + i;
        if(line >= model->summary_count) break;
        canvas_draw_str(canvas, 0, 8 + i * 10, model->summary[line]);
    }
    if(model->summary_count > SUMMARY_LINES_VISIBLE) {
        elements_scrollbar_pos(
            canvas,
            127,
            0,
            50,
            model->summary_offset,
            model->summary_count - SUMMARY_LINES_VISIBLE + 1);
    }
    if(model->has_result) {
        elements_button_right(canvas, "Bits");
    }
}

static void subghz_view_compare_draw_bits(Canvas* canvas, SubGhzViewCompareModel* model) {
    uint16_t total_rows = subghz_view_compare_total_rows(model);
    uint16_t per_cell = model->hex ? 4 : 1;
    uint16_t total_bits = MAX(model->a.bit_count, model->b.bit_count);
    uint16_t first = model->row_offset * CELLS_PER_ROW * per_cell;
    uint16_t last = first + ROWS_VISIBLE * CELLS_PER_ROW * per_cell;
    if(last > total_bits) last = total_bits;

    char header[32];
    snprintf(
        header,
        sizeof(header),
        "%s %u-%u/%u",
        model->hex ? "Hex, bits" : "Bits",
        first,
        last ? last - 1 : 0,
        total_bits);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 8, header);
    char diff[12];
    snprintf(diff, sizeof(diff), "%u diff", model->result.diff_bits);
    canvas_draw_str_aligned(canvas, 124, 0, AlignRight, AlignTop, diff);

    for(uint8_t r = 0; r < ROWS_VISIBLE; r++) {
        uint16_t row = model->row_offset + r;
        if(row >= total_rows) break;
        int32_t y = 20 + r * 24;
        subghz_view_compare_draw_row(canvas, model, &model->a, row, y, "A");
        subghz_view_compare_draw_row(canvas, model, &model->b, row, y + 10, "B");
    }

    if(total_rows > ROWS_VISIBLE) {
        elements_scrollbar_pos(
            canvas, 127, 11, 42, model->row_offset, total_rows - ROWS_VISIBLE + 1);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 63, model->hex ? "OK: bits  <: summary" : "OK: hex  <: summary");
}

static void subghz_view_compare_draw(Canvas* canvas, void* _model) {
    SubGhzViewCompareModel* model = _model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    if(model->page == SubGhzCompareViewPageBits && model->has_result) {
        subghz_view_compare_draw_bits(canvas, model);
    } else {
        subghz_view_compare_draw_summary(canvas, model);
    }
}

static bool subghz_view_compare_input(InputEvent* event, void* context) {
    SubGhzViewCompare* instance = context;
    if(event->key == InputKeyBack) return false;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return true;

    with_view_model(
        instance->view,
        SubGhzViewCompareModel * model,
        {
            if(model->page == SubGhzCompareViewPageSummary) {
                if(event->key == InputKeyUp && model->summary_offset > 0) {
                    model->summary_offset--;
                } else if(
                    event->key == InputKeyDown &&
                    model->summary_offset + SUMMARY_LINES_VISIBLE < model->summary_count) {
                    model->summary_offset++;
                } else if(
                    (event->key == InputKeyRight || event->key == InputKeyOk) &&
                    model->has_result) {
                    model->page = SubGhzCompareViewPageBits;
                }
            } else {
                uint16_t total_rows = subghz_view_compare_total_rows(model);
                if(event->key == InputKeyUp && model->row_offset > 0) {
                    model->row_offset--;
                } else if(event->key == InputKeyDown && model->row_offset + ROWS_VISIBLE < total_rows) {
                    model->row_offset++;
                } else if(event->key == InputKeyLeft) {
                    model->page = SubGhzCompareViewPageSummary;
                } else if(event->key == InputKeyOk) {
                    // Keep the same bits in view when switching between bit and hex rows
                    uint16_t first_bit = model->row_offset * CELLS_PER_ROW * (model->hex ? 4 : 1);
                    model->hex = !model->hex;
                    model->row_offset = first_bit / (CELLS_PER_ROW * (model->hex ? 4 : 1));
                    total_rows = subghz_view_compare_total_rows(model);
                    if(model->row_offset + ROWS_VISIBLE > total_rows) {
                        model->row_offset = total_rows > ROWS_VISIBLE ? total_rows - ROWS_VISIBLE :
                                                                        0;
                    }
                }
            }
        },
        true);
    return true;
}

SubGhzViewCompare* subghz_view_compare_alloc(void) {
    SubGhzViewCompare* instance = malloc(sizeof(SubGhzViewCompare));
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(SubGhzViewCompareModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, subghz_view_compare_draw);
    view_set_input_callback(instance->view, subghz_view_compare_input);
    return instance;
}

void subghz_view_compare_free(SubGhzViewCompare* instance) {
    furi_check(instance);
    view_free(instance->view);
    free(instance);
}

View* subghz_view_compare_get_view(SubGhzViewCompare* instance) {
    furi_check(instance);
    return instance->view;
}

static void subghz_view_compare_add_line(SubGhzViewCompareModel* model, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void subghz_view_compare_add_line(SubGhzViewCompareModel* model, const char* fmt, ...) {
    if(model->summary_count >= SUMMARY_LINES_MAX) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(model->summary[model->summary_count], SUMMARY_LINE_LEN, fmt, args);
    va_end(args);
    model->summary_count++;
}

static void subghz_view_compare_reset_model(SubGhzViewCompareModel* model) {
    model->has_result = false;
    model->summary_count = 0;
    model->summary_offset = 0;
    model->page = SubGhzCompareViewPageSummary;
    model->hex = false;
    model->row_offset = 0;
}

void subghz_view_compare_set_result(
    SubGhzViewCompare* instance,
    const SubGhzCompareCapture* a,
    const SubGhzCompareCapture* b,
    const SubGhzCompareResult* result,
    const char* name_a,
    const char* name_b) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzViewCompareModel * model,
        {
            subghz_view_compare_reset_model(model);
            model->a = *a;
            model->b = *b;
            model->result = *result;
            model->has_result = true;

            if(result->protocol_mismatch) {
                subghz_view_compare_add_line(model, "Protocols differ!");
                subghz_view_compare_add_line(model, "%s vs %s", a->protocol, b->protocol);
            } else {
                subghz_view_compare_add_line(model, "%s, %u bits", a->protocol, a->declared_bits);
            }
            subghz_view_compare_add_line(model, "A: %s", name_a);
            subghz_view_compare_add_line(model, "B: %s", name_b);

            if(result->diff_bits == 0) {
                subghz_view_compare_add_line(
                    model, "Identical over %u bits", result->compared_bits);
            } else {
                subghz_view_compare_add_line(
                    model, "Differ: %u of %u bits", result->diff_bits, result->compared_bits);
                subghz_view_compare_add_line(
                    model,
                    "%u changed run%s, 1st at %u",
                    result->diff_regions,
                    result->diff_regions == 1 ? "" : "s",
                    result->first_diff);
            }
            if(result->longest_fixed_len > 0) {
                subghz_view_compare_add_line(
                    model,
                    "Fixed run: %u-%u (%u)",
                    result->longest_fixed_start,
                    result->longest_fixed_start + result->longest_fixed_len - 1,
                    result->longest_fixed_len);
            }
            if(result->length_mismatch) {
                subghz_view_compare_add_line(
                    model, "Length: %u vs %u bits", a->declared_bits, b->declared_bits);
            }
            if(result->frequency_mismatch) {
                subghz_view_compare_add_line(
                    model,
                    "Freq: %lu.%02lu vs %lu.%02lu",
                    a->frequency / 1000000,
                    (a->frequency % 1000000) / 10000,
                    b->frequency / 1000000,
                    (b->frequency % 1000000) / 10000);
            } else {
                subghz_view_compare_add_line(
                    model,
                    "Freq: %lu.%02lu MHz",
                    a->frequency / 1000000,
                    (a->frequency % 1000000) / 10000);
            }
            if(a->kind == SubGhzCompareKindBinRaw || b->kind == SubGhzCompareKindBinRaw) {
                subghz_view_compare_add_line(model, "BinRAW: 1st block only");
            }
            if(result->truncated) {
                subghz_view_compare_add_line(
                    model,
                    "Cut to %u bits",
                    (a->kind == SubGhzCompareKindKey && b->kind == SubGhzCompareKindKey) ?
                        64 :
                        SUBGHZ_COMPARE_MAX_BITS);
            }
        },
        true);
}

void subghz_view_compare_set_error(SubGhzViewCompare* instance, const char* text) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzViewCompareModel * model,
        {
            subghz_view_compare_reset_model(model);
            // Split the message on newlines into summary lines
            const char* start = text;
            while(*start && model->summary_count < SUMMARY_LINES_MAX) {
                const char* end = strchr(start, '\n');
                size_t len = end ? (size_t)(end - start) : strlen(start);
                if(len >= SUMMARY_LINE_LEN) len = SUMMARY_LINE_LEN - 1;
                memcpy(model->summary[model->summary_count], start, len);
                model->summary[model->summary_count][len] = 0;
                model->summary_count++;
                if(!end) break;
                start = end + 1;
            }
        },
        true);
}
