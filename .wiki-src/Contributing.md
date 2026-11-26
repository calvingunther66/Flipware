# Contributing

Bug reports, device test results and pull requests are all welcome.

## Reporting a bug or a test result

Open an [issue](https://github.com/calvingunther66/flipware/issues). Say which build you ran (the version string from Settings > About, or the commit), what you did, what you expected and what happened. For a crash, include the message the Flipper shows after it restarts.

Results from features marked **untested on device** on [[Features]] are especially useful, whether they work or not.

## Pull request flow

1. Fork the repo (or push a branch, if you have access) and branch off `dev`.
2. Make the change. Keep a PR to one feature or one fix.
3. Run the checks below locally.
4. Open the PR against `dev` and fill in the template: what's new, and the two reviewer boxes. Say plainly whether you tested on a device, and what you didn't test.
5. CI runs **Build** and **Lint** on the PR.
6. A maintainer reviews and merges.

### Rules on `dev`

`dev` is protected by the "Protect dev" ruleset:

- Every change goes through a pull request. No direct pushes, no force pushes, and the branch can't be deleted.
- Commits on `dev` must be signed. If your commits aren't signed, the PR is merged with **Squash and merge**, which produces one signed commit.
- Review conversations must be resolved before merging. Pushing new commits dismisses stale approvals.
- The rules apply to everyone, admins included.

## Code checks

```bash
./fbt lint && ./fbt lint_py     # what CI's Lint job checks
./fbt format && ./fbt format_py # fix formatting
./fbt COMPACT=1 DEBUG=0 updater_package   # make sure it still builds and fits
```

Follow [CODING_STYLE.md](https://github.com/calvingunther66/flipware/blob/dev/CODING_STYLE.md).

## Icons must be 1-bit

The image lint (`scripts/imglint.py`, part of `./fbt lint`) rejects any icon that isn't pure 1-bit black and white. App icons (`fap_icon`) and assets saved as grayscale or with an alpha channel fail. Convert them first, for example:

```bash
python3 -c "from PIL import Image; Image.open('icon.png').convert('1').save('icon.png')"
```

Then check the result by eye: thresholding can drop thin lines.

## Flash space

The firmware image is almost full. Prefer external apps (`.fap`) and on-demand plugins (`.fal`) over code built into the firmware, and check the size of `COMPACT=1 DEBUG=0` builds.

## Porting from other firmwares

Many Flipware features come from Unleashed, the official firmware, RogueMaster and others. When you port something:

- Check the license is compatible (most are GPL-3.0, like Flipware).
- Credit the original author and source release in the PR and at the top of the ported code. Keep them as commit author where you cherry-pick.
- Say in the PR what you changed from the original and why.

## API changes

New public symbols go in `targets/f7/api_symbols.csv` with a minor version bump. Don't remove or change existing symbols: apps built for Momentum rely on them.
