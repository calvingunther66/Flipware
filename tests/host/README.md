# Host tests

Tests that build firmware code with the PC compiler, so they run in seconds and
in CI without a toolchain or a device. Each directory has a `run.sh` that builds
and runs one test; a non-zero exit means it failed.

| Test | What it covers |
| --- | --- |
| `host_auth` | Host sign-in core: known-answer vectors, parser and DER fuzzing, ASan and UBSan |
| `lfrfid_manual_format` | 125 kHz RFID "Add Manually" formats and the protocols behind them |
| `subghz_compare` | Sub-GHz capture compare (the test source sits in `applications/main/subghz/helpers/tests/`) |
| `release_notes` | Release notes are read from the tag's `CHANGELOG.md` section only |

Run them all with `./fbt test_host` (`ARGS="host_auth"` for one) or
`tests/host/run.sh [name...]`. Needs `gcc`, Python 3 with `cryptography`
(`fido2` too if you want the host sign-in client data cross-check; point `PYTHON` at
that interpreter), and the submodules `lib/mbedtls` and `lib/mlib`.
CI: `.github/workflows/host-tests.yml`. The check reports as **`host-tests`** (the job
id, not the workflow name). To make it a required check, use exactly that name in the
`dev` ruleset, and change the ruleset together with the job id if the job is ever renamed.
Its Python packages are pinned (`cryptography>=42,<51`, `fido2>=2.2,<3`) to the versions
the tests were run with.

## Adding a test

1. Make `tests/host/<name>/run.sh` (executable). Build with `-Wall -Wextra -Werror` and
   the sanitizers where the code allows, run the binary, and exit non-zero on failure.
2. For firmware code that includes `<furi.h>`, compile against `tests/host/shim`
   (see its README) instead of pulling in the RTOS.
3. `tests/host/run.sh` finds the new directory by itself, so nothing else needs editing.
