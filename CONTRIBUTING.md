# Contributing to Flipware

Thanks for helping. Flipware is a small project with one maintainer, so short,
focused pull requests get reviewed fastest. The plan for the next year is in
[ROADMAP.md](ROADMAP.md); pick an issue labelled `roadmap` or open one first if
you want to do something bigger.

By contributing you agree that your work is released under the project's
[GPL-3.0 license](LICENSE). Bundled apps and libraries keep their own licenses.

Report security problems privately, not in an issue: see [SECURITY.md](SECURITY.md).

## Branches and pull requests

- `dev` is the integration branch. Branch from it and open your pull request
  against it.
- One pull request does one thing. A fix and an unrelated clean-up are two
  pull requests.
- Fill in [the pull request template](.github/pull_request_template.md). It has
  a "What's new" section and a reviewer checklist; leave the boxes you did not
  do unticked and say why.
- Update the docs in the same pull request (`documentation/`, the app's own
  readme, or `ReadMe.md`).
- Follow [CODING_STYLE.md](CODING_STYLE.md). CI runs the linters and will fail
  on formatting.

## How to build

```bash
git clone --recursive --jobs 8 https://github.com/calvingunther66/Flipware.git
cd Flipware

./fbt updater_package      # build an update package (.tgz) in dist/
./fbt flash_usb_full       # flash a connected Flipper over USB
./fbt launch APPSRC=appid  # build and run one app
./fbt lint_all             # check formatting (C, Python, images)
./fbt format_all           # fix formatting
```

`./fbt` downloads its own toolchain on first use. All targets and options are in
[documentation/fbt.md](documentation/fbt.md). CI builds the `f7` target with
`DEBUG=0 COMPACT=1`; build that way when you compare sizes.

To run the on-device unit tests, see
[documentation/UnitTests.md](documentation/UnitTests.md). Host-side tests live
in `tests/host/`; run them all with `./fbt test_host`, or `tests/host/run.sh` without
the toolchain. CI runs them on every pull request (the Host tests workflow).

## What a pull request needs

Every pull request should have:

1. **A passing Build and Lint run.** Both run on every pull request.
2. **A changelog line** in [CHANGELOG.md](CHANGELOG.md), under Added, Updated or
   Fixed. One line, written for someone who uses the firmware, not for someone
   who reads the diff. Credit the original author when the change is not yours
   (see below). Put it under `## Unreleased` and add the PR number. When a
   release is tagged, that section becomes the release notes for the tag.
3. **The size report reviewed.** Flash is almost full, so a change that grows
   the firmware has to free as much elsewhere or move code to an app on the SD
   card. Build writes a flash and RAM report on every run (job summary, and a
   comment on the pull request): image size, free flash below the radio stack,
   `.data` and `.bss`, the change against the `dev` build the branch started
   from, and the symbols that grew most. Two limits turn Build red, both set in
   [.github/size_budget.json](.github/size_budget.json): free flash may not drop
   below the minimum there (512 bytes for now, so there is a little headroom; it
   goes up as Stage 2 frees space), and a pull request
   may not grow the image by more than 512 bytes unless the maintainer has put the
   `flash-budget` label on it (then re-run the Build job). The label lifts only
   that growth limit, not the free-flash minimum: growth that would take free
   flash under the minimum fails even with the label, so free space elsewhere
   first. A pull request is checked against the limits of the branch
   it targets, so changing `size_budget.json` takes effect once it is merged. The script is
   `scripts/size_report.py`; its test is `tests/host/size_report/run.sh`.
4. **A test or an emulator check.** Add a unit or host test where the code can
   be tested. For anything the emulator can show, run it in
   [FlipperEmu](https://github.com/calvingunther66/FlipperEmu) and tick
   "checked in FlipperEmu": it boots the firmware package you build, models the
   Video Game Module and has checks such as `scripts/lock_check.py` for the PIN
   lock. A change to the lock, host sign-in, U2F or the expansion port needs an
   emulator check.
5. **A device check where it matters.** Tick "uploaded to a device" only if you
   did. The emulator is no substitute for a radio, a card or a real module, and
   a pull request that was only emulated should say so.

## API changes

Apps built for Momentum keep running on Flipware, so the app API is handled
with care. The exported symbols are listed in `targets/f7/api_symbols.csv` and
`targets/f18/api_symbols.csv`.

- Both files must carry the same version (the first lines). CI fails when they
  differ.
- `fbt` bumps the version for you. When your change touches a header listed in
  `SDK_HEADERS`, the build stops and adds the new entries to the `.csv` marked
  `?`. Change each `?` to `+` (exposed to apps) or `-` (not exposed), then build
  again. Commit the `.csv` changes in both targets; CI fails on uncommitted
  changes after the build.
- Do not remove or change a `+` symbol. That needs a major API bump, which is
  agreed with upstream first. Only then raise the major version by hand.
- Flipware keeps Momentum's API version. A difference from the official
  firmware shows up as a CI warning and is expected.

## Crediting upstream authors

Flipware is built on Momentum, Unleashed and the official firmware, and ports
code from them and from other projects. When you bring code in:

- Say where it came from in the commit message (project, release or commit) and
  in the changelog line, with the author's handle, for example
  `UL: Add Doorbell32 protocol (by @author)`.
- Keep the original copyright headers and license files. Check that the license
  is compatible with GPL-3.0 before you copy anything.
- Do not squash several authors into one. Keep authorship in the git history
  where you can.
- Send generic fixes upstream too, so the fork's diff shrinks.

## AI-assisted pull requests

Using an AI assistant to write code, tests or docs is fine. Flipware does not
ban it, and a pull request is not judged by who or what typed it. The usual
rules apply:

- **You own what you submit.** You are responsible for every line, including
  where it came from and its license.
- **Reviewers check it like any other change.** They look at whether the APIs
  and symbols exist in this tree, whether the change is as small as it claims,
  and whether the lock, signing and memory-handling code is right.
- **Show that it works.** A passing build only says it compiles. Say which test,
  emulator check or device check you ran.
- **Keep it reviewable.** Split large machine-written changes into focused
  pull requests.

A `Co-Authored-By` trailer in the commit is welcome but not required.

## Reporting bugs and asking for features

Use the issue templates. Say which firmware build you ran (the version shown in
Settings, or the commit) and attach the debug log if you have one. If the
problem is a security problem, do not open an issue: use [SECURITY.md](SECURITY.md).
