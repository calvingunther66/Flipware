# Video Game Module

Flipware's biggest addition is its work on the official Video Game Module (VGM), which has an RP2040 and plugs into the Flipper's GPIO header. Several of these features need matching firmware on the VGM itself, from the [video-game-module fork](https://github.com/calvingunther66/video-game-module).

## Coprocessor offload

Status: **device-tested** ([#1](https://github.com/calvingunther66/flipware/pull/1)).

The `coproc` service (`applications/services/coproc`) hands compute jobs to the VGM's RP2040 over the expansion link. When no module is attached, the VGM firmware doesn't support the job, or an attempt fails, it falls back to running the job on the Flipper, with the same result.

Try it from the CLI (`./fbt cli`, or any serial terminal):

```
storage crc32 /ext/some_file       # reports whether it was offloaded, and the CRC
storage heatshrink /ext/small_file # heatshrink round trip via coproc (file up to 4000 B), checked against local
```

**What it is good for:** the link is roughly 20 to 100 times slower per byte than the Flipper's own CPU, so CRC32 is a correctness demo, not a speed-up. Offload pays off only for jobs with a small input and a lot of compute, such as keyspace searches. That is future work on the [roadmap](https://github.com/calvingunther66/flipware/blob/dev/ROADMAP.md) (stage 3, coproc v2).

## Faster expansion link

Status: **device-tested** ([#1](https://github.com/calvingunther66/flipware/pull/1)).

The expansion link now receives with DMA into a deeper buffer, runs at 2 Mbaud and fails fast when the module goes away. Developing it also fixed a cross-thread race, a worker stack overflow, a multi-chunk deadlock and a semaphore leak in the stock expansion worker.

## GPIO expander on the VGM breakout

Status: **device-tested** in an integration build ([#2](https://github.com/calvingunther66/flipware/pull/2)). Needs the matching VGM firmware.

The VGM's breakout pins become extra GPIO for the Flipper. Pins are RP2040 GPIO numbers: only **16, 17 and 21 to 29** are accepted, with ADC on **26 to 29**. The VGM refuses every other pin, so the expansion UART and the video pins can't be touched. GPIO 21, 24 and 25 are also the VGM's RGB LED.

CLI:

```
vgm gpio 16 mode out
vgm gpio 16 write 1
vgm gpio 17 mode down
vgm gpio 17 read
vgm gpio 26 adc
```

Modes are `in`, `out`, `up` (pull-up) and `down` (pull-down). `adc` returns the raw 12-bit sample and millivolts.

JavaScript (`require("vgm")`):

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

Each call is one round trip over the expansion link and throws if the VGM doesn't answer or refuses. The same module still has Momentum's IMU functions (`getPitch`, `getRoll`, `getYaw`, `deltaYaw`). Types are in `applications/system/js_app/packages/fz-sdk/vgm/index.d.ts`.

## SD card and ext 3.3V

While a module is attached, Flipware no longer power-cycles the 3.3 V rail on the GPIO header when it mounts the SD card or retries after an SD I/O error.

## VGM desktop over HDMI and USB display stream

Status: **not usable from a release yet.**

The plan is a full desktop on the VGM's HDMI output (live mirror of the Flipper screen, an SD file browser and an app dock, driven by keyboard and mouse), and the same desktop streamed out of the VGM's USB-C port so a computer can act as screen and keyboard. That work lives only on a development branch of the VGM fork: it is unmerged and unreleased, and there is no host viewer for the USB stream yet. It is tracked in stage 3 of the [roadmap](https://github.com/calvingunther66/flipware/blob/dev/ROADMAP.md).
