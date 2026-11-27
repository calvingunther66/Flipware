# Firmware bug triage (roadmap 1.5)

The list of firmware bugs that [FlipperEmu](https://github.com/calvingunther66/FlipperEmu)
found is `docs/FIRMWARE_BUGS.md` there (FW-01 to FW-24). This is where each of them stands in
Flipware, plus the four bugs the roadmap found in the code. Each one is **fixed**, **not
applicable** or **deferred, with a reason**.

How the statuses were reached: from Flipware's source at `dev` (`9d116b9`), by reading it.
**Nothing here has been tried on a real Flipper, and the cloud session that wrote the fixes
could not build the firmware**, so the Build run of the pull request is the compile check. The
column "Check" says what in FlipperEmu's `scripts/fwbugs_check.py` (or another FlipperEmu
script) covers the fix. A fix with no emulator check says why.

Later, on this branch's build: FW-01 was checked on a Flipper, and FW-01, FW-02 and FW-06 in the
emulator. The same emulator run reproduced FW-05.

| No. | Status | What was done | Check |
| --- | --- | --- | --- |
| FW-01 | Fixed | A storage RPC request with no path is answered `ERROR_INVALID_PARAMETERS` (list, info, stat, timestamp, read, rename, backup, tar extract), instead of reaching `strcmp(NULL)` | `fwbugs_check.py`: the 11-byte message |
| FW-02 | Fixed | The ISO14443-3B `dump` callback returns the command it chose (`NfcCommandStop`), not `NfcCommandContinue` | `fwbugs_check.py`: `fuzz nfc --replay` of a 3B card |
| FW-03 | Fixed | `st25tb_get_type_from_uid()` returns `St25tbTypeNum` for a UID that names no known chip; the poller and `st25tb_poller_sync_detect_type()` report `St25tbErrorNotPresent` | none: needs an ST25TB card file the emulator has no helper for |
| FW-04 | Fixed already | The NFC shell's protocol names come from the protocol layer (`nfc_cli_get_protocol_name()`), so a protocol without a hand-made entry (Type 4 Tag, NTAG4xx, EMV's ISO14443-4A) no longer reaches `printf("%s", NULL)`. Came with the Texkom change (`7673bcd`) | `fwbugs_check.py --fuzz`: `fuzz nfc` (not run by CI until FW-05 is fixed) |
| FW-05 | Deferred | Reproduced after the fixes were written: `fuzz nfc --runs 16 --seed 1` ends in `[CRASH][dump] furi_check failed` when the NFC shell's `dump` reads an EMV card. The `flpw-001` release does the same, so it is older than these fixes. Not fixed here; its cause has not been looked for | `fwbugs_check.py --fuzz`: `fuzz nfc` (fails on it) |
| FW-06 | Fixed | `nfc_app_free()` stops the current scene (`scene_manager_stop()`: the scanner and the listener) and the poller before `nfc_free()` checks that the NFC worker is idle | `fwbugs_check.py`: `app NFC`, Read, `app --close` |
| FW-07 | Fixed already | `26bdd91` | |
| FW-08 | Fixed | The slideshow view's draw, input and timer callbacks do nothing while the view has no slideshow, and the exit callback sets the pointer back to NULL after freeing it | none: needs a loaded host, 6 in 50 updates |
| FW-09 | Deferred | `36ef5e9` (finite waits on queue sends from the input callback) may already break the chain, but that was not checked. The Send scene's 100 ms blink still fills the notification queue (eight entries) while charging. Needs a hold of 12 s with the cable in | `./flipperemu press ok --hold 12` on a Send screen, by hand |
| FW-10 | Fixed | The `dump -k` callback fills the member of the event's union that the poller reads (`key_request_data` for a 3DES key, `auth_context.password` for a password) and clears the rest | none: needs an Ultralight C card file with a key |
| FW-11 | Fixed | DESFire record files are read by record count (`record.cur` records, as many as fit a frame at a time), not by a byte count taken for one | none: needs a DESFire card with a record file |
| FW-12 | Fixed | The 32-bit Securakey encoder copies the second half of the facility code from data bit 9, as the decoder and the 26-bit encoder do | none: a unit test needs the RFID test fixtures |
| FW-13 | Fixed | `furi_hal_subghz_start_async_tx()` clears the DMA channel's flags before it enables the interrupts. The emulator sets the half-transfer flag whether or not its interrupt is on (as RM0434 says); if a real chip does otherwise this was never a bug, and the clear is harmless | none |
| FW-14 | Fixed, unconfirmed | `cli_vcp` keeps the two-port USB configuration when it is already set, as the old code did. Flipware's enable path returns early when the CLI is already enabled, so the bug as reported could not be reproduced from the source alone | FlipperEmu's `usb_dual_check.py` switches to its second-port checks when the firmware has two ports; not run here |
| FW-15 | Fixed already | `8e7b990` | |
| FW-16 | Fixed | `crypto encrypt` copies the text into a zeroed buffer of whole blocks | none |
| FW-17 | Fixed | The BLE serial service's RPC status is passed as a four-byte value | none |
| FW-18 | Deferred | `furi_thread_join()` polls every 2 ticks and the idle task sleeps only for 4 or more. Waiting longer in the join, or changing it to a semaphore, changes the latency of every join in the firmware, and what it costs a battery was never measured | |
| FW-19 | Fixed already | Same cause as FW-04: the shell printed the name of a protocol that had none. Type 4 Tag is not in `emulate`'s list of protocols, so it now says "not supported" | `fwbugs_check.py --fuzz`: `fuzz nfc` (not run by CI until FW-05 is fixed) |
| FW-20 | Fixed (JS) | `js_serial_deinit()` waits for the transmission to be complete before it switches the UART off. `furi_hal_serial_deinit()` itself still does not; the other callers (USB-UART bridge, GPS, uart_echo) were left alone | `uart_check.py` has the byte count |
| FW-21 | Fixed | `furi_hal_gpio_init_ex()` sets both edge triggers of an interrupt or event mode to what the mode asks for, so the falling trigger a signal reader left on is cleared | none: needs a SLIX emulation first; the trigger bit is `FTSR1` bit 2 |
| FW-22 | Not applicable | Unleashed's and RogueMaster's MIFARE Plus listener. Flipware's `mf_plus` has a poller and no listener | |
| FW-23 | Fixed | `furi_thread_flags_clear()` is `ulTaskNotifyValueClearIndexed()`, one critical section, instead of a read and a write back | `scripts/fw23_lost_flag.py` (its demonstration is the bug: on this fix the dump returns; `--control` is the comparison) |
| FW-24 | Deferred | Covered by roadmap epic 5.4 (mass storage) | |

## Seen on the official firmware, to verify

FW-03, FW-09, FW-10, FW-11, FW-12, FW-13, FW-16, FW-17, FW-18 and FW-20 are in the table above.
Each was read in Flipware's source, and all of them are there. FW-12 and FW-17 are the only
two whose bytes can be compared by hand: `01 00 00 00` for the RPC status, data bit 9 for the
32-bit Securakey facility code.

## Found in the code

| Where | Status | Reason |
| --- | --- | --- |
| The loader's double start (`loader.c`, the `LoaderStatusErrorAppStarted` comment, FL-3522) | Deferred | A race between two requests to start an app (desktop, menu). Which of them should win, and whether the second should show a message, is a decision about the UI. Needs a repro on a device |
| Files left open on SD unmount (`storage_ext.c`, `sd_unmount_card()`) | Deferred | A user's unmount is already refused while a file is open (`storage_process_sd_unmount()`). The forced one, when the card is pulled out or fails, drops the volume with files still in the open-file list. Closing them is a change to the storage service's bookkeeping that needs a card pulled out under load to check |
| The ignored SPI timeout (`furi_hal_spi.c`, `furi_hal_spi_bus_end_txrx()`) | Deferred | Every busy loop of the blocking SPI calls ignores the timeout, not just this one. Bounding them changes a hot path that every radio and the SD card use, and cannot be tested without hardware |
| A failed battery-gauge read shown as "0 cycles" | Deferred | `BQ27220_ERROR` is 0, so a failed read is a valid-looking value everywhere. The way out is a read that says whether it worked, which is a change to the driver's API. The Power app would then show "?" |
