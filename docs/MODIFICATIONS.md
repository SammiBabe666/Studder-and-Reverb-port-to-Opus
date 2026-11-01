# What the port changes against Hundekraft's files

The originals are in [`third_party/`](../third_party/), unchanged: Hundekraft OP-1 Workshop 3.1.1,
`ext/kennel/synth_sdk/plugins/{reverb,stutter}/`. The full diffs are in [`docs/diffs/`](diffs/). Modified 2026-11-01
by sammi (GPL-3.0 section 5).

**The sound code is unchanged.** Kept as they were:
- **the reverb:** `rv_run`, `mixout`, `lfo_step`, `lfo_taps`, the lazy clear, the init's LFO set-up and the knob
  scaling;
- **STUTTER:** `grid`, `slice_start`, `run`, and the render's order.

The tests show it: the ports' output equals the originals' byte for byte, on the PC and compiled for the Blackfin
([TESTING.md](TESTING.md)).

## Both

| Kennel | The port | Why |
|---|---|---|
| state in `static` arrays per FX position (`rv[3]`, `st[3]`), the buffer from `op1_fx_scratch(pos)` | the state in the instance's `state`, the delay line or ring in its `bulk` (`need()` asks for exactly what is used: 86,652 / 131,072 bytes) | Opus effect types keep nothing of an instance in statics; the core makes one per slot and per manager instance |
| `op1_fx_param_q15(p, i)`: raw filter-slot words through the firmware's range table | a clamp: the parameters are this type's own, 0..32767 | Opus types declare their own limits; kennel's `plugin.json` defaults are already positions 0..32767 and are kept as they are |
| the master: kennel's dispatcher ran a mono effect on `m = (L + R) / 2` and added `y - m` to both sides | `stereo()` does exactly that | the original's master behaviour, kept |
| the FX peak meter word (`op1_fx_meter`) written for the screen | not written | the Opus effect page has no meter |
| an OS SVG screen (`SCREEN_reverb` with Kali artwork by DAN76; `SCREEN_stutter`, a film projector) | `view()`: the core's effect page, with the four values, a picture and a caption | Opus effect types draw through the core's page (`struct opus_fx_view`). The artwork is not used. STUTTER's picture is the original's "projected picture" of the last 16 slices |
| `satb` through the Blackfin builtin unless `OP1_HOST` | the builtin on `__bfin__`, the portable form elsewhere | the same choice keyed on the compiler, so the host tests build |

## The reverb

Nothing else. The peak the render computed for the meter is still computed (`rv_run` returns it) and is not used.

## STUTTER

| Kennel | The port | Why |
|---|---|---|
| blue press: page 2, where blue turns word 5 (REPEATS) | shift + blue turns REPEATS, one of the 6 steps per detent, round and round as kennel's `(word >> 9) % 6` went | an Opus effect type has a shift layer (`shift_knob`) but no press handler |
| orange press: the FREEZE latch, per FX position, not saved (`op1_fx_page[pos][3]`) | shift + orange: clockwise on, anticlockwise off; a static flag per position (`freeze_latch[pos]`), not saved | as above. The latch's semantics are kept: per position, not stored in the sound |
| `op1_beat_clock()` (kennel's SDK inline) | `beat_clock()`, the same reads at the same addresses: the metronome state at `0x01AADABC` (+0x1C length, +0x20 next, +0x14 follows tape) and the tape speed and position at `0xFF802970` / `0xFF802974` | Opus 2 has no tempo service. These are firmware 246's addresses as kennel documents them; they are only read |
| `sx_init(pos)`: the position is the argument | `init()` learns the master from `fx->stereo`; the synth or drum position is learnt at the first block from `opus_instrument_now()` (the dice's seed is the position's, as kennel's) | an Opus init gets the instance, not the position |
| the ring cleared in init with 32-bit stores | the same | it is heard: REVERSE while frozen reads the part's end before it is recorded |
| `rec` left to the zeroed statics | reset in init | instance memory is not zero after a re-init; `rec` is only read once a freeze sets it, so this changes nothing heard |
| `sx_blocks[]`, `s->latch` | `sx_blocks` dropped (unused by the effect); `latch` kept | |

## Added

- `need`, `init`, `mono`, `stereo`, `view`, `shift_knob` and `opl_init`: the Opus 2 effect type around the
  original's calls.
- `memset` and `memcpy`, in case the compiler calls them: the core exports neither.
