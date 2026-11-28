# Sub-GHz protocols as SD card plugins

Roadmap 2.3, issue [#47](https://github.com/calvingunther66/Flipware/issues/47).
Written as the design spike; the first step of it is implemented. What is done, and
what is next, is at the end.

## Why

The Sub-GHz app already runs from the SD card, but its protocol library stays in the
firmware image: the app reaches it through the API table. Flash is nearly full (about 660
bytes free, with a floor of 512), and 29 decoders had been commented out of
`protocol_items.c` to make room: 25 weather stations, the Schrader TPMS sensor, the
POCSAG pager, X10 and Hormann BiSecur.

A plugin is a `.fal` file, an ELF the app loads into RAM with the same `PluginManager`
that the NFC app uses for its card parsers. Code in a plugin costs no firmware flash,
only SD card space and, while it is loaded, RAM.

## What stays in the firmware

- The registry types and functions (`lib/subghz/registry.c`, `receiver.c`,
  `transmitter.c`, `environment.c`), the protocol base and the block helpers
  (`lib/subghz/blocks`). Plugins call them through the API table.
- `subghz_protocol_registry` and every protocol it holds now. The registry is an exported
  variable, and the command line (`subghz rx`, `decode_raw`), the JS `subghz` module,
  Seed Capturer, the unit tests and third-party apps decode with it. Taking protocols out
  of it would change what they do, so that is a separate decision (see "Next").
- Every protocol that has a function in `targets/f7/api_symbols.csv` (`*_create_data`,
  `subghz_protocol_marantec_crc8`, the RAW and BinRAW helpers, `secplus_v1_check_fixed`).
  Add Manually and ProtoPirate link against those.

One symbol was added to the API: `subghz_block_generic_serialize_common`, which the
weather, TPMS and pager "generic" helpers use to write the first lines of a `.sub` file
(API 87.4 to 87.5, one table entry, no new code).

## What is a plugin

`applications/main/subghz/plugins/` holds seven bundles. Each is one `.fal`, one
`PLUGIN` app in `application.fam` (`subghz_plugin_<id>`, `fal_embedded`, requires
`subghz`), and one entry point that lists its protocols:

| Bundle | Protocols | Modulation | `.fal` on the SD card | RAM when loaded |
| --- | --- | --- | --- | --- |
| `ws1` | inFactory-TH, ThermoPRO-TX4, Nexus-TH, GT-WT02, GT-WT03, Acurite-606TX, Acurite-609TXC, Acurite-986, Bresser-3CH | AM | 25.2 KiB | 9.3 KiB |
| `ws2` | LaCrosse_TX, TX141THBv2, Oregon2, Oregon3, Acurite 592TXR, Ambient_Weather | AM | 22.1 KiB | 8.8 KiB |
| `ws3` | Solight TE44, Auriol HG06061, Oregon-v1, TX8300, Wendox W6726, Auriol AHFL, Kedsum-TH, EMOS E601x, Acurite 5n1, Vauno-EN8822C | AM | 27.0 KiB | 10.0 KiB |
| `tpms` | Schrader GG4 | AM | 8.2 KiB | 3.3 KiB |
| `x10` | X10 | AM | 4.0 KiB | 1.0 KiB |
| `pager` | POCSAG | FM | 6.8 KiB | 2.2 KiB |
| `bisecur` | Hormann BiSecur | FM | 7.4 KiB | 2.4 KiB |

Sizes are from an `arm-none-eabi-gcc` 13 build of this branch (the CI toolchain gives
slightly different numbers), RAM is the sections the loader allocates. The three weather
bundles each carry a copy of `ws_generic.c` (1.6 KiB), because plugins cannot share code
with each other; one bundle would save 3 KiB but could not be loaded in parts.

The AM set (everything a default `AM650` preset loads) is about 32 KiB of RAM; the FM set
is under 5 KiB.

The plugin side is `plugins/subghz_protocol_plugin.h`: a plugin returns a
`SubGhzProtocolPlugin` (a list of `SubGhzProtocol` pointers in feed order) through the
usual `FlipperAppPluginDescriptor`.

The bundles were chosen so that the order the decoders see pulses is the one the
firmware list had (Bresser 3CH before LaCrosse, Solight TE44 before Auriol), and so that
nothing but the loader's table has to know which protocol is where.

## The loader

`helpers/subghz_plugins.c` lives in the Sub-GHz app, so it costs no firmware flash.
Its table, `helpers/subghz_plugins_table.c`, has one row per bundle: id, file name,
modulation and the protocol names inside it. It is what lets the app find the bundle of
a protocol without loading all of them.

- **Registry.** The loader builds a `SubGhzProtocolRegistry` of the firmware protocols
  followed by the protocols of the loaded bundles, and the app gives that to the
  environment. Everything that already looks protocols up by name or index (receiver,
  transmitter, Add Manually through `subghz_transmitter_alloc_init`) works unchanged.
- **By modulation.** The app asks for the bundles that suit the preset in use: AM bundles
  for `AM270` and `AM650`, FM bundles for `FM238`, `FM476`, `FM12K` and custom presets
  with FM, FSK or MSK in the name. Bundles for the other modulation are unloaded. Not by
  frequency: the receiver hops between 315, 433 and 868 MHz with the same preset, and a
  decoder that is not in the loaded set would miss signals while it does.
- **When.** At start of the app (default preset), when the preset is set with the radio
  idle (receiver settings, loading a `.sub` file), and when receiving starts. A
  change while the radio is in use waits for the next start of receiving.
- **Rebuilding the receiver.** A receiver has one decoder per protocol, and those decoders
  run plugin code. So the receiver is always freed first, the plugins change, and a new
  receiver is allocated from the new registry, with the callback and filters it had. The
  history of decoded signals keeps strings, not decoders, so it is untouched.
- **Opening a saved file.** A `.sub` file whose protocol lives in a bundle that is not
  loaded (an FM file while an AM preset is set, or a bundle that was disabled) loads that
  one bundle for the file and keeps it until the app closes.
- **RAM guard.** Before a bundle is loaded the loader checks the free heap against twice
  the file size plus 16 KiB. A bundle that does not fit is skipped and logged
  (`SubGhzPlugins`), and the app keeps running with what is loaded.
- **Disabling bundles.** `/ext/subghz/assets/disabled_plugins.txt` lists bundle ids to
  skip, separated by spaces or new lines, for example `ws2 ws3`. A saved file still loads
  its bundle.
- **Failure.** A missing or broken `.fal` is logged and skipped: the app has fewer
  protocols, not a crash. Protocols that are not found are reported the way they always
  were.

## Receive-only

The bundles are the decoders and the encoders that sit in the same source files.
Three of the protocols (Nexus-TH, Acurite 592TXR and Hormann BiSecur) carry the `Send`
flag from their sources; nothing was changed there. A saved
file of one of them can be sent while its bundle is loaded, because the transmitter finds
it in the same registry. The roadmap asks for receive-only, and that is what is tested.

## Adding or moving a protocol

1. Put the sources in `plugins/<bundle>/` (a new directory for a new bundle).
2. Add the protocol to the bundle's `*_plugin.c`, in feed order.
3. Add its name to the bundle's row in `helpers/subghz_plugins_table.c`. For a new
   bundle, add the row, an `App()` in `application.fam` named `subghz_plugin_<id>`,
   and a path in the unit test's list.
4. Run `tests/host/subghz_plugins/run.sh`. It checks the table against the headers, the
   entry points and `application.fam`, and that no protocol is in both a plugin and
   `protocol_items.c`.
5. If the plugin needs a firmware function that is not in `api_symbols.csv`, the build
   says `Symbols not resolved using firmware's API`. Prefer a function that is already
   exported; each new one costs a table entry.

## Tests

- `tests/host/subghz_plugins`: the table, the sources and the manifest agree.
- Unit tests (`applications/debug/unit_tests/tests/subghz/subghz_test.c`,
  `subghz_plugins_decoders_test`): loads the seven `.fal` files from the SD card, checks
  that all 29 protocols are in the registry next to the firmware ones, and decodes the
  captures that exist for Bresser-3CH (two), Solight TE44, Vauno-EN8822C, Acurite 592TXR
  and Hormann BiSecur. The old, disabled copies of these tests are gone.
- Not covered yet: decoders without a capture (most of the weather stations, TPMS,
  POCSAG, X10). Sub-GHz captures of them are welcome in `unit_tests/subghz`.

## Next

Flash is the point of the roadmap item, and this step does not free any: the 29 decoders
were already out of the image. The next step moves decoders that are built in now.

Measured on the same build, 42 of the 59 built-in protocols have no function in the API
table. Their code is about 52 KiB (`came`, `nice_flo`, `princeton`, `linear`, `holtek`,
`honeywell_wdb`, `magellan`, `dooya`, the `keyfinder`s, and so on). Moving them as
bundles of a few KiB each would free up to that much firmware flash, against 664 bytes
free today, and would cover the "flash freed is measured in the size report" part of #47.

That step changes what the firmware registry holds, so it needs a decision first:

- **A. Shrink `subghz_protocol_registry`.** Most flash, but the command line, the JS
  module, Seed Capturer and third-party apps lose those decoders unless they load
  plugins too. The command line and Seed Capturer are ours; JS and external apps are
  not.
- **B. Keep the registry whole, add a second one.** `subghz_protocol_registry` keeps
  what it has; nothing is freed.
- **C. Shrink it and add a helper.** Same as A, plus a small exported function that
  returns the composed registry (loads the plugins), so the command line, JS and
  third-party apps can opt in with one call. It costs a few hundred bytes of firmware
  flash.

Recommended: C, after this step has been run on a device. Also open:

- A settings screen for the disabled list (today it is a text file).
- A per-bundle RAM readout, if the 32 KiB AM set turns out to be too much next to the
  app on a real device (the 2.5 RAM work is changing what the app holds).
- Splitting `ws_generic.c` out is not possible without plugin-to-plugin linking; if the
  duplicated 3 x 1.6 KiB matters, merge the weather bundles.
