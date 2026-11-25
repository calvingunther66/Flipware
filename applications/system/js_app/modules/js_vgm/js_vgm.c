#include "../../js_modules.h"
#include <furi.h>
#include <toolbox/path.h>
#include <coproc/coproc.h>
#include "imu.h"

#define TAG "JsVgm"

typedef struct {
    Imu* imu;
    bool present; // IMU answered at module load
    Coproc* coproc;
} JsVgmInst;

#define JS_VGM_GPIO_TIMEOUT_MS (500UL)

static void js_vgm_get_pitch(struct mjs* mjs) {
    mjs_val_t obj_inst = mjs_get(mjs, mjs_get_this(mjs), INST_PROP_NAME, ~0);
    JsVgmInst* vgm = mjs_get_ptr(mjs, obj_inst);
    furi_assert(vgm);

    if(vgm->present) {
        mjs_return(mjs, mjs_mk_number(mjs, imu_pitch_get(vgm->imu)));
    } else {
        FURI_LOG_T(TAG, "VGM not present.");
        mjs_return(mjs, mjs_mk_undefined());
    }
}

static void js_vgm_get_roll(struct mjs* mjs) {
    mjs_val_t obj_inst = mjs_get(mjs, mjs_get_this(mjs), INST_PROP_NAME, ~0);
    JsVgmInst* vgm = mjs_get_ptr(mjs, obj_inst);
    furi_assert(vgm);

    if(vgm->present) {
        mjs_return(mjs, mjs_mk_number(mjs, imu_roll_get(vgm->imu)));
    } else {
        FURI_LOG_T(TAG, "VGM not present.");
        mjs_return(mjs, mjs_mk_undefined());
    }
}

static void js_vgm_get_yaw(struct mjs* mjs) {
    mjs_val_t obj_inst = mjs_get(mjs, mjs_get_this(mjs), INST_PROP_NAME, ~0);
    JsVgmInst* vgm = mjs_get_ptr(mjs, obj_inst);
    furi_assert(vgm);

    if(vgm->present) {
        mjs_return(mjs, mjs_mk_number(mjs, imu_yaw_get(vgm->imu)));
    } else {
        FURI_LOG_T(TAG, "VGM not present.");
        mjs_return(mjs, mjs_mk_undefined());
    }
}

static float distance(float current, float start, float angle) {
    // make 0 to 359.999...
    current = (current < 0) ? current + 360 : current;
    start = (start < 0) ? start + 360 : start;

    // get max and min
    bool max_is_current = current > start;
    float max = (max_is_current) ? current : start;
    float min = (max_is_current) ? start : current;

    // get diff, check if it's greater than 180, and adjust
    float diff = max - min;
    bool diff_gt_180 = diff > 180;
    if(diff_gt_180) {
        diff = 360 - diff;
    }

    // if diff is less than angle return 0
    if(diff < angle) {
        FURI_LOG_T(TAG, "diff: %f, angle: %f", (double)diff, (double)angle);
        return 0;
    }

    // return diff with sign
    return (max_is_current ^ diff_gt_180) ? -diff : diff;
}

static void js_vgm_delta_yaw(struct mjs* mjs) {
    mjs_val_t obj_inst = mjs_get(mjs, mjs_get_this(mjs), INST_PROP_NAME, ~0);
    JsVgmInst* vgm = mjs_get_ptr(mjs, obj_inst);
    furi_assert(vgm);
    size_t num_args = mjs_nargs(mjs);
    if(num_args < 1 || num_args > 2) {
        mjs_prepend_errorf(
            mjs,
            MJS_BAD_ARGS_ERROR,
            "Invalid args. Pass (angle [, timeout]). Got %zu args.",
            num_args);
        mjs_return(mjs, mjs_mk_undefined());
        return;
    }

    if(!vgm->present) {
        FURI_LOG_T(TAG, "VGM not present.");
        mjs_return(mjs, MJS_UNDEFINED);
        return;
    }

    double angle = mjs_get_double(mjs, mjs_arg(mjs, 0));
    if(isnan(angle)) {
        mjs_prepend_errorf(mjs, MJS_TYPE_ERROR, "Invalid arg (angle).");
        mjs_return(mjs, mjs_mk_undefined());
        return;
    }
    uint32_t ms = (num_args == 2) ? mjs_get_int(mjs, mjs_arg(mjs, 1)) : 3000;
    uint32_t timeout = furi_get_tick() + ms;
    float initial_yaw = imu_yaw_get(vgm->imu);
    do {
        float current_yaw = imu_yaw_get(vgm->imu);
        double diff = distance(current_yaw, initial_yaw, angle);
        if(diff != 0) {
            mjs_return(mjs, mjs_mk_number(mjs, diff));
            return;
        }
        furi_delay_ms(100);
    } while(furi_get_tick() < timeout);

    mjs_return(mjs, mjs_mk_number(mjs, 0));
}

// Whether a module is attached and its link is up (what offload needs).
static void js_vgm_present(struct mjs* mjs) {
    JsVgmInst* vgm = JS_GET_CONTEXT(mjs);
    furi_assert(vgm);
    mjs_return(mjs, mjs_mk_boolean(mjs, coproc_is_available(vgm->coproc)));
}

static const char* js_vgm_gpio_status_name(uint8_t status) {
    switch(status) {
    case CoprocGpioStatusBadPin:
        return "not a VGM breakout pin (16, 17, 21-29)";
    case CoprocGpioStatusBadOp:
        return "unknown op";
    case CoprocGpioStatusBadArg:
        return "bad argument";
    case CoprocGpioStatusNoAdc:
        return "pin has no ADC (26-29 only)";
    case CoprocGpioStatusNotOutput:
        return "pin is not an output, call gpioMode(pin, \"out\") first";
    default:
        return "unknown status";
    }
}

// Runs one CoprocTaskGpio record. Pin comes from arg 0. On any failure it
// sets a JS error and returns false; the caller must then just return.
static bool js_vgm_gpio_run(struct mjs* mjs, uint8_t op, uint8_t arg, uint16_t* value) {
    JsVgmInst* vgm = JS_GET_CONTEXT(mjs);
    furi_assert(vgm);

    mjs_val_t pin_arg = mjs_arg(mjs, 0);
    if(!mjs_is_number(pin_arg)) {
        JS_ERROR_AND_RETURN_VAL(mjs, MJS_BAD_ARGS_ERROR, false, "pin must be a number");
    }
    const int pin = mjs_get_int(mjs, pin_arg);
    if(pin < 0 || pin > 255) {
        JS_ERROR_AND_RETURN_VAL(mjs, MJS_BAD_ARGS_ERROR, false, "pin out of range");
    }

    // The pin is passed through as is: the VGM's whitelist decides.
    const uint8_t record[COPROC_GPIO_RECORD_SIZE] = {op, (uint8_t)pin, arg};
    uint8_t result[COPROC_GPIO_RECORD_SIZE] = {0};
    size_t result_size = sizeof(result);
    const bool ok = coproc_run(
        vgm->coproc,
        CoprocTaskGpio,
        record,
        sizeof(record),
        result,
        &result_size,
        JS_VGM_GPIO_TIMEOUT_MS,
        NULL);
    if(!ok || result_size != COPROC_GPIO_RECORD_SIZE) {
        JS_ERROR_AND_RETURN_VAL(mjs, MJS_INTERNAL_ERROR, false, "no answer from the VGM");
    }
    if(result[0] != CoprocGpioStatusOk) {
        JS_ERROR_AND_RETURN_VAL(
            mjs, MJS_BAD_ARGS_ERROR, false, "GPIO %d: %s", pin, js_vgm_gpio_status_name(result[0]));
    }

    if(value) *value = (uint16_t)(result[1] | (result[2] << 8));
    return true;
}

static void js_vgm_gpio_mode(struct mjs* mjs) {
    mjs_val_t mode_arg = mjs_arg(mjs, 1);
    if(mjs_nargs(mjs) != 2 || !mjs_is_string(mode_arg)) {
        JS_ERROR_AND_RETURN(
            mjs, MJS_BAD_ARGS_ERROR, "Pass (pin, \"in\" | \"out\" | \"up\" | \"down\")");
    }
    const char* mode_str = mjs_get_cstring(mjs, &mode_arg);
    uint8_t mode;
    if(!strcmp(mode_str, "in")) {
        mode = CoprocGpioModeInput;
    } else if(!strcmp(mode_str, "out")) {
        mode = CoprocGpioModeOutput;
    } else if(!strcmp(mode_str, "up")) {
        mode = CoprocGpioModePullUp;
    } else if(!strcmp(mode_str, "down")) {
        mode = CoprocGpioModePullDown;
    } else {
        JS_ERROR_AND_RETURN(mjs, MJS_BAD_ARGS_ERROR, "mode must be in, out, up or down");
    }

    if(!js_vgm_gpio_run(mjs, CoprocGpioOpMode, mode, NULL)) return;
    mjs_return(mjs, MJS_UNDEFINED);
}

static void js_vgm_gpio_write(struct mjs* mjs) {
    mjs_val_t level_arg = mjs_arg(mjs, 1);
    if(mjs_nargs(mjs) != 2 || !(mjs_is_boolean(level_arg) || mjs_is_number(level_arg))) {
        JS_ERROR_AND_RETURN(mjs, MJS_BAD_ARGS_ERROR, "Pass (pin, level: boolean)");
    }
    const bool level = mjs_is_boolean(level_arg) ? mjs_get_bool(mjs, level_arg) :
                                                   mjs_get_int(mjs, level_arg) != 0;

    if(!js_vgm_gpio_run(mjs, CoprocGpioOpWrite, level ? 1 : 0, NULL)) return;
    mjs_return(mjs, MJS_UNDEFINED);
}

static void js_vgm_gpio_read(struct mjs* mjs) {
    if(mjs_nargs(mjs) != 1) JS_ERROR_AND_RETURN(mjs, MJS_BAD_ARGS_ERROR, "Pass (pin)");
    uint16_t value = 0;
    if(!js_vgm_gpio_run(mjs, CoprocGpioOpRead, 0, &value)) return;
    mjs_return(mjs, mjs_mk_boolean(mjs, value != 0));
}

// Millivolts, like gpio's readAnalog(). 12-bit ADC, 3.3 V reference.
static void js_vgm_gpio_adc(struct mjs* mjs) {
    if(mjs_nargs(mjs) != 1) JS_ERROR_AND_RETURN(mjs, MJS_BAD_ARGS_ERROR, "Pass (pin)");
    uint16_t value = 0;
    if(!js_vgm_gpio_run(mjs, CoprocGpioOpAdc, 0, &value)) return;
    const uint32_t mv = (uint32_t)value * 3300UL / 4095UL;
    mjs_return(mjs, mjs_mk_number(mjs, mv));
}

static void* js_vgm_create(struct mjs* mjs, mjs_val_t* object, JsModules* modules) {
    UNUSED(modules);
    JsVgmInst* vgm = malloc(sizeof(JsVgmInst));
    vgm->coproc = furi_record_open(RECORD_COPROC);
    vgm->imu = imu_alloc();
    vgm->present = imu_present(vgm->imu);
    if(!vgm->present) FURI_LOG_W(TAG, "VGM not present.");
    mjs_val_t vgm_obj = mjs_mk_object(mjs);
    mjs_set(mjs, vgm_obj, INST_PROP_NAME, ~0, mjs_mk_foreign(mjs, vgm));
    mjs_set(mjs, vgm_obj, "getPitch", ~0, MJS_MK_FN(js_vgm_get_pitch));
    mjs_set(mjs, vgm_obj, "getRoll", ~0, MJS_MK_FN(js_vgm_get_roll));
    mjs_set(mjs, vgm_obj, "getYaw", ~0, MJS_MK_FN(js_vgm_get_yaw));
    mjs_set(mjs, vgm_obj, "deltaYaw", ~0, MJS_MK_FN(js_vgm_delta_yaw));
    mjs_set(mjs, vgm_obj, "present", ~0, MJS_MK_FN(js_vgm_present));
    mjs_set(mjs, vgm_obj, "gpioMode", ~0, MJS_MK_FN(js_vgm_gpio_mode));
    mjs_set(mjs, vgm_obj, "gpioWrite", ~0, MJS_MK_FN(js_vgm_gpio_write));
    mjs_set(mjs, vgm_obj, "gpioRead", ~0, MJS_MK_FN(js_vgm_gpio_read));
    mjs_set(mjs, vgm_obj, "gpioAdc", ~0, MJS_MK_FN(js_vgm_gpio_adc));
    *object = vgm_obj;
    return vgm;
}

static void js_vgm_destroy(void* inst) {
    JsVgmInst* vgm = inst;
    furi_assert(vgm);
    imu_free(vgm->imu);
    vgm->imu = NULL;
    furi_record_close(RECORD_COPROC);
    vgm->coproc = NULL;
    free(vgm);
}

static const JsModuleDescriptor js_vgm_desc = {
    "vgm",
    js_vgm_create,
    js_vgm_destroy,
    NULL,
};

static const FlipperAppPluginDescriptor plugin_descriptor = {
    .appid = PLUGIN_APP_ID,
    .ep_api_version = PLUGIN_API_VERSION,
    .entry_point = &js_vgm_desc,
};

const FlipperAppPluginDescriptor* js_vgm_ep(void) {
    return &plugin_descriptor;
}
