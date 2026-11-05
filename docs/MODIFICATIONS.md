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
| an OS SVG screen (`SCREEN_reverb` with Kali artwork by DAN76; `SCREEN_stutter`, a film projector) | `view()`: the core's effect page with the four values; the picture (1.1.0: the defective tape head, the endless mirror) drawn under the OS's screen with `opus_on_frame_under` | Opus effect types draw through the core's page; the stock effects' layer lets the OS's off overlay dim the picture natively. The artwork is not used. STUTTER keeps the original's last 16 slices along the bottom |
| `satb` through the Blackfin builtin unless `OP1_HOST` | the builtin on `__bfin__`, the portable form elsewhere | the same choice keyed on the compiler, so the host tests build |

## The reverb

Nothing else. The peak the render computed for the meter is still computed (`rv_run` returns it) and is not used.

## STUTTER

| Kennel | The port | Why |
|---|---|---|
| blue press: page 2, where blue turns word 5 (REPEATS) | shift + blue turns REPEATS, one of the 6 steps per detent, round and round as kennel's `(word >> 9) % 6` went | an Opus effect type has a shift layer (`shift_knob`) but no press handler |
| orange press: the FREEZE latch, per FX position, not saved (`op1_fx_page[pos][3]`) | shift + orange: clockwise on, anticlockwise off; a static flag per position (`freeze_latch[pos]`), not saved | as above. The latch's semantics are kept: per position, not stored in the sound |
| `op1_beat_clock()` (kennel's SDK inline) | `beat_clock()`, the same reads at the same addresses: the metronome state at `0x01AADABC` (+0x1C length, +0x20 next, +0x14 follows tape) and the tape speed at `0xFF802970` (1.2: kennel's tape position read at `0xFF802974` is gone, nothing used it; the speed is read only while the clock follows the tape) | Opus 2 has no tempo service. These are firmware 246's addresses as kennel documents them; they are only read, and since 1.2 only trusted while they behave like a clock (Added, below) |
| `sx_init(pos)`: the position is the argument | `init()` learns the master from `fx->stereo`; the synth or drum position is learnt at the first block from `opus_instrument_now()` (the dice's seed is the position's, as kennel's) | an Opus init gets the instance, not the position |
| the ring cleared in init with 32-bit stores | the same | it is heard: REVERSE while frozen reads the part's end before it is recorded |
| `rec` left to the zeroed statics | reset in init | instance memory is not zero after a re-init; `rec` is only read once a freeze sets it, so this changes nothing heard |
| `sx_blocks[]`, `s->latch` | `sx_blocks` dropped (unused by the effect); `latch` kept | |

## Added

- 1.2.0, STUTTER: the clock guard. Nothing in Opus vouches for the beat clock's addresses, and a later core may keep
  other data there, so each block the reading must behave like a clock: at the same beat length, the time to the
  next beat counts down by 0 to 2 steps (a clock updated in another thread) or wraps once at the beat. A jump, a
  reading that stands still for 4 blocks while it should move, or an impossible value counts against it; a new
  beat length a little. A stuck reading is set aside in about 0.1 s, one that changes every block in about 0.2 s;
  the grid then runs on alone at the last tempo a trusted clock gave (else 120 BPM), and a real clock comes back
  by itself. A real clock is never set aside by tape restarts, tempo turns or late updates (tests/host), and with
  it the sound is the original's byte for byte (the differential tests, on the PC and in the Blackfin simulator).
  It costs about 100 cycles a block (under 1%). `-DSTUTTER_NO_DEEP` builds with no deep reads at all (the grid
  always at 120 BPM): tools/build.sh writes it as `nodeep/sammi.stutter.opmod`, the fallback for a core where the
  addresses are known not to hold the clock.

- 1.1.0: the per-position page state the audio publishes after each block (REVERB: the input's peak and the loop's
  level, read from `lp1`/`lp2`; STUTTER: the slice, part, read position and mode it already keeps). Read-only: the
  sound does not read them back, and the output stays byte-identical (the tests).

- `need`, `init`, `mono`, `stereo`, `view`, `shift_knob` and `opl_init`: the Opus 2 effect type around the
  original's calls.
- `memset` and `memcpy`, in case the compiler calls them: the core exports neither.
