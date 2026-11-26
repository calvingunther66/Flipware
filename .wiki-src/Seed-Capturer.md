# Seed Capturer

Status: **untested on device.** It builds and passes the app checks, but nobody has run it against a real FAAC, Genius or Erreka remote yet. From [#21](https://github.com/calvingunther66/flipware/pull/21), a port of the app by xMasterX from Unleashed unlshd-094.

## What it does

Some rolling-code remotes (FAAC SLH, Genius, Erreka) use a seed. One Fix + Hop pair from a single press fits far too many possible seeds, so recovering the seed offline needs several Hops under the same Fix. Seed Capturer collects them.

It is **receive-only**: it never transmits and never tries to solve a seed itself.

## How to use it

1. Apps > Sub-GHz > **Seed Capturer**.
2. Pick the remote type (FAAC SLH, Genius or Erreka) and the frequency (433.92 or 868.35 MHz).
3. Press the remote a few times. The app locks onto the first remote's Fix and keeps every distinct Hop that arrives under it, from 2 up to 10.
4. Save.

Each capture is written to:

```
/ext/apps_data/subghz_seed_captures/<Mfr>_<Fix>_<timestamp>.txt
```

It is a plain Flipper-format text file with the Fix, the Hops in order, the frequency and the preset, ready for a solver to read later.

## Notes

- Only use it on remotes you own or are allowed to test.
- If the radio can't start (no device, or an invalid frequency), the app now cleans up properly instead of leaving the radio registry initialised.
- Please report results, good or bad, in [Issues](https://github.com/calvingunther66/flipware/issues).
