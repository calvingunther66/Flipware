The files imu.c, imu.h, and ICM42688P are from https://github.com/flipperdevices/flipperzero-game-engine.git

Please see that file for the license.

## GPIO expander (Flipware)

`present()`, `gpioMode()`, `gpioWrite()`, `gpioRead()` and `gpioAdc()` drive the
VGM's breakout GPIO through the coproc `CoprocTaskGpio` task
(`applications/services/coproc/coproc.h`). They need the matching VGM firmware.

- Pins are RP2040 GPIO numbers. Only the breakout pins 16, 17 and 21-29 are
  accepted (ADC on 26-29). The VGM refuses every other pin, so the expansion
  UART and the DVI pins can't be touched.
- GPIO 21, 24 and 25 are also the VGM's RGB LED.
- Each call is one round trip over the expansion link and throws if the VGM
  doesn't answer or refuses the request.

```js
let vgm = require("vgm");
if (vgm.present()) {
    vgm.gpioMode(16, "out");
    vgm.gpioMode(17, "down");
    vgm.gpioWrite(16, true);
    print(vgm.gpioRead(17)); // true with 16 and 17 jumpered
    print(vgm.gpioAdc(26), "mV");
}
```

The CLI has the same thing: `vgm gpio <pin> <mode|write|read|adc> [arg]`.
