#include "coproc.h"

#include <furi.h>
#include <toolbox/cli/cli_command.h>
#include <toolbox/cli/cli_registry.h>
#include <cli/cli.h>
#include <lib/toolbox/args.h>

// `vgm` CLI: drives the Video Game Module's breakout GPIO through
// CoprocTaskGpio. One record per command. The pin is passed through
// unchecked on purpose: the module's whitelist is what refuses it, so a
// refused pin here proves that check works.

#define COPROC_CLI_GPIO_TIMEOUT_MS (500UL)

static void coproc_cli_print_usage(void) {
    printf("Usage:\r\n");
    printf("vgm gpio <pin> <cmd> [arg]\r\n");
    printf("  pin: RP2040 GPIO on the VGM breakout (16, 17, 21-29; ADC on 26-29)\r\n");
    printf("  cmd:\r\n");
    printf("    mode <in|out|up|down>  set direction / pull\r\n");
    printf("    write <0|1>            drive an output pin\r\n");
    printf("    read                   read the pin level\r\n");
    printf("    adc                    raw 12-bit ADC sample and mV\r\n");
}

static const char* coproc_cli_gpio_status_name(uint8_t status) {
    switch(status) {
    case CoprocGpioStatusOk:
        return "ok";
    case CoprocGpioStatusBadPin:
        return "refused: not a VGM breakout pin";
    case CoprocGpioStatusBadOp:
        return "refused: unknown op";
    case CoprocGpioStatusBadArg:
        return "refused: bad argument";
    case CoprocGpioStatusNoAdc:
        return "refused: pin has no ADC (26-29 only)";
    case CoprocGpioStatusNotOutput:
        return "refused: pin is not an output (vgm gpio <pin> mode out)";
    default:
        return "unknown status";
    }
}

static bool coproc_cli_parse_mode(const char* word, uint8_t* mode) {
    if(!strcmp(word, "in") || !strcmp(word, "input")) {
        *mode = CoprocGpioModeInput;
    } else if(!strcmp(word, "out") || !strcmp(word, "output")) {
        *mode = CoprocGpioModeOutput;
    } else if(!strcmp(word, "up") || !strcmp(word, "pullup") || !strcmp(word, "pull-up")) {
        *mode = CoprocGpioModePullUp;
    } else if(!strcmp(word, "down") || !strcmp(word, "pulldown") || !strcmp(word, "pull-down")) {
        *mode = CoprocGpioModePullDown;
    } else {
        return false;
    }
    return true;
}

static void coproc_cli_gpio(FuriString* args) {
    FuriString* word = furi_string_alloc();
    int pin = 0;
    uint8_t record[COPROC_GPIO_RECORD_SIZE] = {0};

    do {
        if(!args_read_int_and_trim(args, &pin) || pin < 0 || pin > 255) {
            coproc_cli_print_usage();
            break;
        }
        if(!args_read_string_and_trim(args, word)) {
            coproc_cli_print_usage();
            break;
        }

        record[1] = (uint8_t)pin;
        const char* cmd = furi_string_get_cstr(word);
        if(!strcmp(cmd, "mode")) {
            record[0] = CoprocGpioOpMode;
            if(!args_read_string_and_trim(args, word) ||
               !coproc_cli_parse_mode(furi_string_get_cstr(word), &record[2])) {
                coproc_cli_print_usage();
                break;
            }
        } else if(!strcmp(cmd, "write")) {
            int level = 0;
            record[0] = CoprocGpioOpWrite;
            if(!args_read_int_and_trim(args, &level) || (level != 0 && level != 1)) {
                coproc_cli_print_usage();
                break;
            }
            record[2] = (uint8_t)level;
        } else if(!strcmp(cmd, "read")) {
            record[0] = CoprocGpioOpRead;
        } else if(!strcmp(cmd, "adc")) {
            record[0] = CoprocGpioOpAdc;
        } else {
            coproc_cli_print_usage();
            break;
        }

        Coproc* coproc = furi_record_open(RECORD_COPROC);
        uint8_t result[COPROC_GPIO_RECORD_SIZE] = {0};
        size_t result_size = sizeof(result);
        const bool ok = coproc_run(
            coproc,
            CoprocTaskGpio,
            record,
            sizeof(record),
            result,
            &result_size,
            COPROC_CLI_GPIO_TIMEOUT_MS,
            NULL);
        furi_record_close(RECORD_COPROC);

        if(!ok || result_size != COPROC_GPIO_RECORD_SIZE) {
            printf(
                "No answer from the VGM (not attached, or its firmware lacks the GPIO task)\r\n");
            break;
        }

        const uint8_t status = result[0];
        const uint16_t value = (uint16_t)(result[1] | (result[2] << 8));
        if(status != CoprocGpioStatusOk) {
            printf("GPIO %d: %s\r\n", pin, coproc_cli_gpio_status_name(status));
            break;
        }

        switch(record[0]) {
        case CoprocGpioOpRead:
            printf("GPIO %d: %u\r\n", pin, value);
            break;
        case CoprocGpioOpAdc:
            printf(
                "GPIO %d: adc %u (%lu mV)\r\n", pin, value, (unsigned long)value * 3300UL / 4095UL);
            break;
        default:
            printf("GPIO %d: ok\r\n", pin);
            break;
        }
    } while(false);

    furi_string_free(word);
}

static void coproc_cli(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(pipe);
    UNUSED(context);
    FuriString* cmd = furi_string_alloc();

    if(args_read_string_and_trim(args, cmd) && furi_string_cmp_str(cmd, "gpio") == 0) {
        coproc_cli_gpio(args);
    } else {
        coproc_cli_print_usage();
    }

    furi_string_free(cmd);
}

void coproc_on_system_start(void) {
#ifdef SRV_CLI
    CliRegistry* registry = furi_record_open(RECORD_CLI);
    cli_registry_add_command(registry, "vgm", CliCommandFlagDefault, coproc_cli, NULL);
    furi_record_close(RECORD_CLI);
#else
    UNUSED(coproc_cli);
#endif
}
