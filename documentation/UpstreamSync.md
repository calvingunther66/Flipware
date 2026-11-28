# Independence note

Flipware started as a fork of Momentum. On 2026-11-27 it was split from that
project: the repository left the fork network, there is no upstream remote, and
nothing in Flipware tracks, merges from or sends anything to another firmware
project. This page says where Flipware stood at the split and how to bring in a
fix from a public source by hand, if that is ever wanted.

## Where Flipware stood at the split

- Last merge from the projects it was built on: 2026-06-01. Momentum `dev`
  at `33671b9`, plus Unleashed `unlshd-093` and `unlshd-094` ports. On
  2026-11-27 the maintainer judged Flipware up to date with Momentum; that
  was not checked against Momentum's repository when this note was written.
- Everything newer in those projects is not in Flipware, and no one is
  watching for it.
- Momentum's changelog up to that point is kept for reference in
  [upstream/MomentumChangelog.md](upstream/MomentumChangelog.md). It is not part
  of Flipware's release notes.

## What this means

- **No sync and no cadence.** There is no monthly merge and no sync branch.
- **No upstream remotes.** The repository has only `origin`. Don't add others
  to the project's checkouts; use a throwaway clone for reading (below).
- **No outbound contributions.** Flipware doesn't open pull requests, issues or
  comments on other projects' repositories.
- **Two submodules still live elsewhere.** `applications/external` (bundled apps)
  and `assets/protobuf` are pinned to commits in other projects' repositories.
  A pin only moves in a pull request that says so; see
  [Bundled apps](#bundled-apps).
- **Compatibility stays.** Flipware keeps the app API version in
  `targets/*/api_symbols.csv` and still defines `FW_ORIGIN_Momentum`, so apps
  that exist today keep running. Exported symbols are removed only at a
  deliberate API major bump decided in a Flipware issue (see
  [CONTRIBUTING.md](../CONTRIBUTING.md)).

## Taking a fix from a public source by hand

Do this only for a change that is worth its cost: a security fix, a crash, or
a feature someone here asked for. Port the change, not the history.

1. **Read it in a throwaway clone**, outside the Flipware checkout, so no
   remote is left behind. Find the commit or pull request that holds the change.
2. **Check the license.** Flipware is GPL-3.0. Don't copy anything that isn't
   compatible, and keep the original copyright header.
3. **Apply it on a normal Flipware branch.**
   `git format-patch -1 <commit>` in the throwaway clone, then `git am -3` or
   `git apply --3way` in Flipware. Patches often miss because Flipware has
   changed the same files; fix up by hand and keep the intent.
4. **Credit the author.** Name the project, release or commit and the author's
   handle in the commit message and the changelog line, for example
   `UL: Add Doorbell32 protocol (by @author)`. Keep authorship in the git
   history where you can (see "Crediting upstream authors" in
   [CONTRIBUTING.md](../CONTRIBUTING.md)).
5. **Mind the files that conflict most:**
   - `targets/*/api_symbols.csv`: keep Flipware's table, add only the symbols
     the change needs as `+` or `-`, commit both targets, and let `fbt` bump the
     version.
   - `assets/packs`, `applications/main/nfc`, the lfrfid, subghz and ibutton
     libraries and apps;
   - `furi_hal_nfc*`, `furi_hal_sd.c` and `furi_hal_power.c`;
   - `loader.c`, `gui.c`, `view_dispatcher.c` and `cli_vcp.c`;
   - `elf_file.c` and the updater flasher.
6. **Pass the same gates as any change:** the flash and RAM report (free flash
   must stay above the 512-byte floor), the host tests and the emulator checks.
   Flash is tight, so a ported feature may need space freed first or may belong
   on the SD card as an app or plugin.
7. **Put it in the changelog** under Unreleased, like any other change.

## Bundled apps

`applications/external` is a submodule of an app collection hosted by another
project. The bundled apps are built into every Flipware update package, so a
bump changes what ships.

- The pin stays where it is until someone bumps it on purpose, in its own pull
  request. Check that the apps still build (`./fbt fap_dist`) and that the ones
  you changed boot in the emulator.
- Bumping is not part of any regular routine. Do it for a fix, a missing app or
  an API break, and say why in the pull request.
- If Flipware ever needs to patch an app, host a plain copy of that app
  collection under the project's account and point `.gitmodules` at it. Don't
  use GitHub's Fork button, which would put the copy back in the fork network.
- `assets/protobuf` is handled the same way. Its pin moves only when the RPC
  definitions Flipware ships change.

## Hand-port history

Ports made so far came from Unleashed (`unlshd-093`, `unlshd-094`: Doorbell32,
KeyFinder2, Texkom keys, RFID facility-code formats, Seed Capturer, infrared
universal remote save). They are credited in [CHANGELOG.md](../CHANGELOG.md).
