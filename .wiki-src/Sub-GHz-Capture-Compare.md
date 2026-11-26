# Sub-GHz Capture Compare

Status: **device-tested** ([#18](https://github.com/calvingunther66/flipware/pull/18)).

Line two saved captures of a remote up bit by bit, to see which part of the code stays fixed and which part rolls, without copying `Key:` lines onto a computer.

## How to use it

1. Sub-GHz > **Saved**, open a saved signal.
2. Choose **Compare with...**.
3. Pick a second `.sub` file. The file browser starts in the same folder.

## What you see

**Summary** (scroll with Up and Down):
- protocol and bit count, and the two file names
- `Differ: n of m bits`
- `k changed runs, 1st at i`
- `Fixed run: s-e (len)`, the longest stretch that didn't change
- frequency, plus any warnings

Press **Right** or **OK** for the **bit view**: two A/B pairs of 16 cells per screen, with differing bits drawn inverted. Up and Down scroll, **OK** switches between bits and hex (a nibble is inverted if any of its bits differ), and **Left** goes back to the summary.

**Example:** two presses of a remote with a 16-bit fixed serial and 8 rolling bits show `Differ: 6 of 24 bits`, `3 changed runs, 1st at 16`, `Fixed run: 0-15 (16)`.

## What it can compare

- **Decoded signals (Key files):** the low `Bit:` bits of the 64-bit `Key:`, first-sent bit first. Protocols longer than 64 bits are compared on the 64 bits the Key holds, and the summary says so.
- **BinRAW:** the first `Data_RAW:` block, up to 512 bits ("1st block only").
- **RAW:** not supported. Pulse timings start at arbitrary points, so picking a RAW file shows a message asking for a decoded or BinRAW capture instead.

Different protocols, bit counts or frequencies are flagged in the summary, not refused. Different bit counts are compared over the shorter one, aligned at the first bit.

## Tips

- Capture the same button twice to see the rolling part, or two buttons once each to see which bits are the button code.
- The comparison only reads files. It never transmits.
