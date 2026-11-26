<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset=".github/assets/flipware_logo_dark.png">
    <source media="(prefers-color-scheme: light)" srcset=".github/assets/flipware_logo_light.png">
    <img alt="Flipware" src=".github/assets/flipware_logo_light.png" width="400">
  </picture>
</p>

<h3 align="center">Open-source custom firmware for Flipper Zero</h3>

Flipware is an independent, open-source firmware for the Flipper Zero. It is
built on [Momentum](https://github.com/Next-Flip/Momentum-Firmware), which in
turn builds on the [Official Firmware](https://github.com/flipperdevices/flipperzero-firmware)
and [Unleashed](https://github.com/DarkFlippers/unleashed-firmware). It keeps
everything Momentum offers and adds its own work on expansion hardware, with its
own look and its own release rules.

Flipware is not affiliated with Flipper Devices Inc. or the Momentum team.
"Flipper Zero" is a trademark of Flipper Devices Inc. and is used here only to
say which device this firmware runs on.

## What Flipware adds

- **Video Game Module desktop.** The VGM can show a full desktop over HDMI: a
  live mirror of the Flipper screen, a file browser for the SD card and an app
  dock, driven by keyboard and mouse.
- **USB display stream.** The same desktop streams out of the VGM's own USB-C
  port, so a computer can act as its screen and keyboard.
- **Coprocessor offload.** The Flipper can hand compute jobs to the VGM's
  RP2040 over a faster expansion link (DMA receive, 2 Mbaud).

Everything from Momentum is still here: asset packs, the main menu and lock
screen customisation, Bad KB, the extended Sub-GHz and NFC tools, and the rest
of its [feature list](https://github.com/Next-Flip/Momentum-Firmware#list-of-changes).

Apps built for Momentum keep working: Flipware keeps Momentum's app API and
still defines `FW_ORIGIN_Momentum` for apps that check for it.

## Roadmap

What is planned for the next year, stage by stage, is in
[ROADMAP.md](ROADMAP.md). Each stage has a tracking issue labelled `roadmap`.

## Wiki

How to use each Flipware feature, which ones have been tested on a device,
how to install and build, and how to contribute are in the
[wiki](https://github.com/calvingunther66/flipware/wiki).

## Install

Releases are published on the
[releases page](https://github.com/calvingunther66/Flipware/releases).

> <details><summary><code>qFlipper package (.tgz)</code></summary><ul>
>   <li>Download the .tgz from the latest release</li>
>   <li>Open <a href="https://flipperzero.one/update">qFlipper</a> and connect your Flipper</li>
>   <li>Click <code>Install from file</code>, select the .tgz and wait for the update to finish</li>
> </ul></details>

> <details><summary><code>Zipped archive (.zip)</code></summary><ul>
>   <li>Download the .zip from the latest release and extract it</li>
>   <li>Copy the extracted folder to <code>SD/update</code> on the Flipper</li>
>   <li>On the Flipper, press <code>Down</code> to open the file browser, open that folder and run <code>Update</code></li>
> </ul></details>

## Build it yourself

```bash
git clone --recursive --jobs 8 https://github.com/calvingunther66/Flipware.git
cd Flipware

# Flash over USB (Flipper connected, qFlipper closed)
./fbt flash_usb_full

# Build an update package (.tgz)
./fbt updater_package

# Build and launch one app
./fbt launch APPSRC=your_appid
```

Version tags use the `flpw-` prefix; untagged builds report `flpw-dev`.

## Credits

Flipware exists because of the people who built what it stands on:

- [Flipper Devices](https://github.com/flipperdevices/flipperzero-firmware), the Official Firmware
- [Unleashed](https://github.com/DarkFlippers/unleashed-firmware)
- [Momentum](https://github.com/Next-Flip/Momentum-Firmware) and Xtreme before it
- The authors of every bundled app, credited in each app's folder and license

## License

Flipware is free software under the [GNU General Public License v3.0](LICENSE),
the same license as the firmware it is based on. Bundled apps and libraries keep
their own licenses (MIT, BSD, Apache and others), found next to their code. The
STM32WB wireless stack in `lib/stm32wb_copro` is STMicroelectronics' binary under
its own license (SLA0044).
