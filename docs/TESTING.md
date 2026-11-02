# Tests and results (1.1.0, 2026-11-02)

What was done, kept apart by kind, as it happened. Nothing below claims more than was run. 1.1.0 adds the pages'
pictures ([VISUALS.md](VISUALS.md)) and corrects the `core` line to 2.2; every test below was run again on 1.1.0. The logs are in
[`results/`](../results/): `host_tests.txt`, `reverb_analysis.txt`, `bfin_sim.txt`, `build.txt`.

| Kind | Status |
|---|---|
| 1. Compile and link with the Opus plugin kit (`plugin_tool.py build`, every warning an error) | **done, both** |
| 2. Metadata and file structure read back as the core reads it (`plugin_tool.py show`) | **done, both** |
| 3. `.opmod` format (the kit's own writer and reader; no renamed `.knl` or `.op1p`) | **done, both** |
| 4. Host tests under AddressSanitizer + UndefinedBehaviorSanitizer | **pass** |
| 5–8. Audio at minimum, maximum and between; init, bypass, parameter changes, repeated loading; the reverb's decay, tone, stereo and wet/dry; every STUTTER mode, rate, repeat count, the clock's cases | **pass** (host and simulator) |
| 9. Worst-case CPU and memory | **simulator figures and an estimate only**; not measured on a device |
| 10. Conflicts with other Opus effects and plugin managers | checked on paper (names, ids, controls, type count); **not loaded beside other plugins** |
| 11. Opus emulator | **not done**: the kit's emulator is not released yet ("What the kit does not have yet", plugin-kit README) |
| 12. A physical OP-1 | **not done**: no OP-1 was available |
| 13. The pages (1.1.0): drawn only on their own page, within their box, encoder colours only, bounded cost, still when no audio runs, every STUTTER mode drawn differently | **pass** (host; cost also in the simulator) |
| 14. The pages under the OS's off overlay | **by construction** (the stock effects' layer); previewed with a simulated overlay, **not seen on a device** |

## The toolchain

`bfin-elf-gcc` 13.3.0 with binutils 2.42 (the espressif fork's tag `binutils-2_42`), built from git sources with
`tools/toolchain/build_toolchain.sh` (from Project_Hollow_Saint): ftp.gnu.org could not be reached from the build
machine. That is the compiler version the Opus kit names. The simulator is binutils-gdb's `sim/bfin`, built from the
same tree with `tools/toolchain/build_sim.sh`.

The Opus plugin kit is LizardMan245/opus at commit `d7f28b0` (2026-10-31). Hundekraft's source is OP-1 Workshop 3.1.1
(`hundekraft-3.1.1-source.tar.gz`, sha256 `831f0aa0…299f9e`, the published checksum; Workshop commit `d30fc52`).

## 1–3. Build and read-back

```
sammi.reverb 1.1.0, 7,052 bytes: image 4,680 bytes, zero fill 192, 110 relocations
    imports: fw_snprintf, opus_effect_register, opus_fx_owner, opus_fx_page_shown, opus_instrument_now,
             opus_on_frame_begin, opus_on_frame_under, opus_page_fill, opus_page_frame, opus_page_line
    core 2.2, cpu unmeasured, no uses lines
sammi.stutter 1.1.0, 11,568 bytes: image 8,652 bytes, zero fill 248, 131 relocations
    imports: fw_snprintf, opus_effect_register, opus_fx_owner, opus_fx_page_shown, opus_instrument_now,
             opus_on_frame_begin, opus_on_frame_under, opus_page_dot, opus_page_fill, opus_page_frame,
             opus_page_line, opus_sincos
    core 2.2, cpu unmeasured, uses: blue_turn shift modal, orange_turn shift modal
```

No warnings. Neither imports a `place` or any `fw_*` name but `fw_snprintf` (the page's text). 1.0.0's STUTTER
called `opus_fx_owner` and `opus_instrument_now` (core 2.2, the managers' service) with a `core 2.0` line, so a core
2.0 or 2.1 would have refused it with "needs opus_fx_owner"; 1.1.0 says `core 2.2` for both. STUTTER's beat-clock
read is a plain memory read at firmware 246's addresses, so it shows in no import. It is declared in its manifest's
comment and in the README.

## 4. Host tests (`tests/host/run.sh`, ASan + UBSan, `-fno-sanitize-recover=all`)

**Differential, against Hundekraft's original.** `orig_runner.c` compiles Hundekraft's `reverb.c` and `stutter.c`
**unchanged**, using op1r's host mocks, with kennel's dispatcher routing for the master. `port_runner.c` compiles the
ports and calls them only through their registered `struct opus_effect`, as the core does, including FREEZE through
the shift layer. Both play the same scenarios (`scenario.h`):

- **Reverb:** 8 parameter sets on each of the 3 positions (the defaults, all 0, all max, all middle, and mixed
  extremes), each with 380 blocks of input and 520 blocks of tail. Then every knob moving every block, like an LFO,
  followed by two inits in a row.
- **STUTTER:**
  - every rate × mode × repeat count (144 combinations) at chance 100 %;
  - shape × chance at 0, 1, middle, 32766 and 32767 in every mode;
  - tempo at 30, 40, 240, 120 and 133 BPM, the clock lost and back, the clock following the tape at half speed,
    stopped and double, and a jump of the clock;
  - no clock at all;
  - FREEZE on mid-slice, held across mode changes, off and on again;
  - knobs and REPEATS moving every block, with three re-inits.
- Every input mixes saw notes, noise bursts, silence and full-scale squares. The memory of every instance is filled
  with garbage before every init, which is stricter than the core: it clears memory once and re-inits on top of
  stale contents.

```
PASS differential reverb: port == Hundekraft original, 4,147,200 bytes
PASS differential stutter: port == Hundekraft original, 11,390,464 bytes
```

The comparison found one real difference on the way, and it was fixed: an early draft of the port skipped kennel's
ring clear in STUTTER's init. REVERSE while frozen reads the end of the part before it is recorded, which is silence
after the clear and garbage without it. The clear is back, exactly as kennel does it.

**Port checks** (`port_checks.c`): reverb 3,669 checks, STUTTER 4,872, all pass (1.0.0's counts were higher only
because the old page's waveform and caption were checked point by point). They cover:

- **registration:** name, size, kennel's defaults, limits, needs;
- **the page:** 81 knob combinations; every string lowercase, as the OP-1's font needs, and within its buffer; every
  rate and mode name; the fade and gate readouts at their ends; the caption;
- **the shift layer:** REPEATS 2 → 16 → 2 round and round, a turn under a detent, big turns; FREEZE for every owner
  0–16, each landing on the right position only;
- **mix 0** gives the input back exactly;
- **the master keeps the dry stereo image:** L − R out equals L − R in, at every sample;
- **silence:** typical settings decay to exact digital silence. At the top of time the idle residue stays bounded and
  never grows (see Known limitations);
- **the synth and drum instances sharing one bulk,** as the core gives them: after the synth played, the
  instrument's change and re-init give the drum exactly a fresh instance's output, for both effects;
- **determinism** after a re-init;
- **STUTTER, sample by sample, on a marked input** (each sample says which input sample it came from), for every
  rate × mode × repeat count at 120 BPM:
  - the slice length (22,050 / 11,025 / 7,350 / 5,512 / 3,675 / 2,756 samples) and the part length;
  - the slices landing on the grid;
  - the mode's law away from the fades: repeat = part 0 again; reverse = part 0 backwards; brake = part 0 read at
    100 %, 88 %, 76 % … down to 30 %; gate = the live sound while open;
- **STUTTER's other cases:**
  - chance 0 gives the input back exactly;
  - FREEZE keeps the frozen part across 9 slices;
  - no clock, and the tape stopped while followed, both run on at 120 BPM;
  - at 30 BPM a 1/4 slice is cut to the ring, with no out-of-bounds access (ASan).

**The pages (1.1.0)**, in the same checks: both effects play a real input (with clicks for onsets) on all three
positions through 4 (REVERB) or 8 (STUTTER, every mode) settings while the core's frame sequence runs every 23
blocks (frame start, the drawing under the OS's screen, the page's view):
- 3,120 and 6,240 frames drawn, all within x 88–232, y 14–134 (clear of the corner text, inside the OP-1's window);
- only the four encoder colours and the two greys, and all four encoder colours used;
- at most 154 (REVERB) and 89 (STUTTER) primitives a frame;
- with no audio block running (as when the OS stops calling a switched-off effect), the picture is perfectly still
  frame after frame, and REVERB's goes back to the idle tunnel (no reflections, no orange);
- STUTTER's four modes draw differently;
- nothing is drawn without an effect page, for another owner's page, or once the plugin's view stops being called
  (another type's page).

**Analysis** (`analysis.py`, on the port's renders; times assume 44.1 kHz at the effect's input):

```
RT60 (s), time 0 / 16000 / 32767 (rows) x tone 0 / 20000 / 32767:   1.61 1.81 1.82 | 2.79 3.12 3.17 | 15.7 8.8 14.2
tail brightness (dB, 3 kHz+ vs 100 Hz-1 kHz):                        2.0 8.4 9.5 | 0.5 7.7 9.1 | -1.3 7.0 8.6
echo density, diffusion 0 -> max: 0.072 -> 0.146
mix 0 / 8192 / 16384 / 32767: dry gain 1.000 / 0.692 / 0.384 / -0.229, wet -inf / -7.6 / -1.6 / +4.3 dB
```

The checks: RT60 rises with time, the tail brightens with tone, echo density rises with diffusion, the dry share falls
and the wet share rises with mix, and the master keeps L − R. All 12 pass. Kennel's notes say the OS's mono effect
path may run at a lower rate after R246's decimator. If so, every time is longer on the device, for the original
and the port alike.

## 5–8 and 9. The compiled code in the Blackfin simulator (`tests/bfin_sim/run.sh`)

The ports are compiled with the kit's exact flags (`-mcpu=bf524-any -O2 -ffreestanding -fno-builtin`) and run
bare-metal in `sim/bfin` (model bf524), with the core's names stubbed. They play the same scenarios, on the same
dirty memory. STUTTER's clock is written at the firmware's addresses (`0x01AADABC`, the tape speed at
`0xFF802970`), so the port's own deep reads run.

```
PASS blackfin reverb: the compiled port == Hundekraft's original, 4,147,200 bytes
PASS blackfin stutter: the compiled port == Hundekraft's original, 11,390,464 bytes
```

The reference here is the original compiled for the PC. The compiled port gives the same bytes; that covers the
Blackfin's `satb` builtin, the 64-bit divisions from libgcc and the deep reads.

**A compiler bug found on the way.** At `-O2`, bfin-elf-gcc 13.3 vectorised a loop of
`(int16_t)((b * k * 32) & 32767)` in the test scenarios into paired 16-bit multiplies in `(IS)` mode, which saturate
where C wraps; the simulator showed the knob values clipped. The plugins' own code has no 16-bit-result multiply. The
only saturating instructions in their disassembly are the reverb's intended `satb` shifts, and every `(IS)` multiply
has a 32-bit result, which cannot saturate for 16 × 16 operands. Only the scenario functions are kept from being
vectorised (`SCENARIO_FN`). The plugin code in the runner keeps the kit's flags. This bug is worth knowing for anyone
building Opus plugins with this compiler.

**Cycles per 64-sample block** in the simulator (its instruction timing only; it models no SDRAM or cache):

| | synth / drum (mono), worst | mean | master (stereo), worst | mean | init, worst |
|---|---|---|---|---|---|
| reverb | 15,004 | 12,960 | 17,808 | 15,734 | 175 |
| STUTTER | 12,762 | 9,087 | 15,557 | 10,407 | 32,868 (the ring clear) |

1.1.0's audio side adds the page's levels: about 320 cycles a block for REVERB (the input's peak), 30 for STUTTER.
The page, a frame (the plugin's own drawing code and view, timed with the core's drawing calls stubbed): REVERB worst
1,459, mean 1,100 cycles; STUTTER worst 2,256, mean 1,670. That is the display thread's time, not the audio
block's; the core's renderer then draws the primitives.

**On a device these will be higher.** The kit says each 32-byte line of ordinary memory an audio block touches costs
about 107 cycles.
- **Reverb:** about 34 read and write streams in its delay line, each 2–3 lines a block, so roughly 9–11k more:
  **about 25–30k worst per block**, 4–5 % of the 580,525-cycle block.
- **STUTTER:** its ring touches about 10 lines a block, so **about 14–17k**.
- These are estimates, so neither plugin has a `cpu.txt`. The core then holds each to a tenth of the block (58,052
  cycles, switched off at 25 % over), which is about twice the estimates.
- STUTTER's init clears its 128 KB ring at once, as kennel's does (32,868 cycles in the simulator, plus the SDRAM
  stores' cost on a device). The core runs an effect's init when it is chosen, a sound loads, or the slot or
  instrument changes.
- **To measure on the OP-1:** turn developer mode on. Play the heaviest case: the reverb at time, tone and diffusion
  at maximum on the master with a busy mix; STUTTER at 1/32 in brake with chance 100 %. Read "audio: worst N" in the
  plugin's details (orange push on its row), write `cycles N` into `plugins/sammi.X/cpu.txt`, and build again.

**Memory** (from `need()`): the reverb 86,652 bytes of bulk per instance; STUTTER 131,072. The synth and drum slots
share one bulk, and the master has its own, so the OS's slots take about 174 KB (reverb) or 262 KB (STUTTER), plus
the images (3 KB, 6 KB). Each extra instance a manager makes adds one bulk. Neither asks for fast memory.

## 10. Conflicts (checked on paper, not on a device)

- **Plugin ids:** `sammi.reverb` and `sammi.stutter`. They are distinct from Opus's own (`opus.*`) and from
  `sammi.hollow_saint`.
- **Effect names:** `hkreverb` and `hkstutter`. They differ from the stock effects (delay, grid, punch, phone, spring,
  filter, nitro, cwo), from the starter pack's types (bitcrush, chorus, plate; filter is the stock one shown), and
  from the names opus.bridge hosts Hundekraft's effects under (`reverb`, `stutter`). So both plugins can be loaded
  beside the bridge.
  - A sound saved with Hundekraft's own reverb names `reverb` and still loads through the bridge, not through these
    plugins.
- **Effect type count:** the core takes 8 plugin effect types in all. These two plus the starter pack's three come
  to five.
- **Controls:** the reverb takes none. STUTTER's two shift turns are `modal` and never clash with another plugin's
  controls; on its own effect page the core sends them to its shift layer first. Plain turns stay the OS's.
- **Managers:** both are ordinary effect types (no `cpu user`). They keep nothing of an instance in their statics
  except STUTTER's FREEZE flag and page mirror, which are per position, as kennel's were.

## Previews (1.1.0)

`tools/previews.sh` renders the pages from the plugins' own code on the PC (stand-in rasteriser, stand-in corner text,
simulated off overlay): `docs/media/*.png`, `*.gif`. See [VISUALS.md](VISUALS.md#what-the-previews-are). They are
renderings, not captures from an OP-1.

## What to try on the OP-1 (and then record with a `checked device` line in each manifest)

1. **Load:** both rows say "loaded", with developer mode off as well as on.
2. **Reverb:**
   - on the synth: hold a chord and sweep each knob; time to max gives a long tail;
   - on the drum: the same;
   - on the master: the dry stereo image is kept;
   - at mix 0 it should be silent: the dry sound only.
3. **STUTTER:**
   - at 120 BPM, chance 100 %, each rate and mode;
   - shift + blue through 2…16 parts;
   - shift + orange to freeze, then unfreeze;
   - change the tempo, play and stop the tape, use MIDI clock if you have it.
4. **The pages:** the picture sits clear of the corner values; it moves with the sound; T3 off dims it under the
   OS's overlay like a stock effect's, T3 on restores it; turning knobs while off changes only what the overlay
   covers (as on a stock page).
5. **Living with the rest of the OP-1:** switch sounds, presets and instruments; save; record to tape; disk mode;
   switch off and on.
6. **Next to other plugins:** the starter pack and opus.bridge.
7. **CPU:** the measurement above.
