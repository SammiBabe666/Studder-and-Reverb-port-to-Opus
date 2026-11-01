# The controls

Both effects are effect types in the OS's own effect list, so they go wherever an OP-1 effect goes: the synth's effect
slot, the drum's, the mixer's master, and any Opus manager that runs effect types (opus.overdrive's second effect
behind every slot, for example). T3 switches the effect on and off, as for a stock effect. The four encoders turn
its four parameters on its effect page; shift + a turn is the OS's fine step (STUTTER's blue and orange are the
exceptions, below). An LFO can move any of the four.

Every value below is taken from Hundekraft's source, not assumed: the knob names, the ranges and the scaling are the
original's, and the defaults are its `plugin.json`'s.

## sammi.reverb: `hkreverb`

A fixed-point port of the Mutable Instruments Elements reverb (Griesinger / Dattorro tank) by Hundekraft. Four input
allpasses, the first smeared by an LFO, feed a loop of two halves, each with two allpasses, a low-pass and a delay.
The long delay is modulated by a slow LFO. The tank runs at half rate; the dry path runs at full rate. A soft knee
above -2.5 dBFS keeps the output from clipping hard. The tank is mono, as in the original: both loop outputs are
summed.

| Encoder | Name on the page | What it does | Range (the original's) | Default |
|---|---|---|---|---|
| blue | time | decay: the loop gain per pass | 0.35 to 0.98 | 16000 (48) |
| green | tone | the loop's low-pass: dark to bright | 0.3 to 0.95 (at full rate; the same cutoff at half rate) | 20000 (60) |
| white | diffusion | the allpasses' gain: grainy to smooth | 0.5 to 0.75 | 16384 (49) |
| orange | mix | dry to wet | 0 to 1 | 12000 (36) |

The page shows the four values from 0 to 99, a picture of the tail (an impulse decaying through the loop gain and the
loop's low-pass, as time and tone set them), and the caption "hundekraft reverb".

**On the master** the tank hears the mono sum (L + R) / 2. What it adds is put in the middle, on both sides, and the
dry stereo image stays exactly as it was. This is how kennel ran its mono effects on the master; it is the original's
behaviour, not a new one.

**Times:** at the top of time the tail is very long: an RT60 of several seconds, and up to about 85 s before it
reaches digital silence. See "Known limitations" in the README for the small idle residue there.

## sammi.stutter: `hkstutter`

A beat-locked stutter by Hundekraft. The OP-1's beat clock cuts time into slices. At each slice a dice roll decides
whether the slice plays through or stutters. A stuttering slice plays and records its first part, then repeats that
part until the slice ends, so the repeats always land on the grid.

| Encoder | Name on the page | What it does | Steps | Default |
|---|---|---|---|---|
| blue | rate | the slice length | 1/4, 1/8, 1/8t, 1/16, 1/16t, 1/32 of a beat | 17152 (1/16) |
| green | mode | **repeat**: plain repeats. **brake**: each repeat 12 % slower and lower, down to 30 %, a tape stop. **reverse**: the repeats backwards. **gate**: no repeats, the slice chopped into short bursts of the live sound | 4 | 2304 (repeat) |
| white | shape | the edge softness of each repeat: 2 to 12 ms fades. In **gate**: the open time, 10 to 90 % | continuous | 8000 |
| orange | chance | how many slices stutter: none to all | continuous | 12000 (36 %) |
| **shift + blue** | (caption) "N parts" | **REPEATS**: how many parts a stuttering slice is cut into. One step per detent, round and round | 2, 3, 4, 6, 8, 16 | 4 |
| **shift + orange** | (caption) "freeze" | **FREEZE**: clockwise on, anticlockwise off. Keeps repeating the part playing now, across slices, until you switch it off | on / off | off |

- **REPEATS** is the sound's parameter 5, saved with the sound, as kennel saved its page-2 word.
- **FREEZE** is per effect position (synth, drum, master) and is not saved with the sound. That is kennel's
  behaviour too.
- On kennel, REPEATS was "push blue for page 2, then turn blue", and FREEZE was "push orange". An Opus effect type has
  a shift layer but no encoder-press handler, so the port puts them on shift + turn. The mods list shows them on the
  plugin's controls page (green push). They are declared `modal`, so they never clash with another plugin's controls.

The page shows:
- rate, mode, shape (the fade in ms, or the gate's open time in %) and chance;
- the original's "projected picture": the last 16 slices, newest at the right. A slice that stuttered is a tall bar
  (repeat full, brake stepping down, reverse slanted, gate dashed); one that played through is a short stub;
- the caption: the number of parts, and "freeze" while frozen.

**Tempo:** the grid follows the OP-1's tempo: the BPM setting or MIDI clock, through the stock metronome's beat
clock, whether the metronome is heard or not. While the clock follows the tape, it also follows the tape's speed.
When there is no clock (the tape stopped while the clock follows it, or no tempo yet) the grid runs on at 120 BPM by
itself.

**Limits:** the ring buffer is 65,536 samples (1.49 s). A slice longer than that (a 1/4 slice under 40 BPM) is cut
short. **On the master** STUTTER hears the mono sum (L + R) / 2 and its change goes to both sides, as on kennel: it
repeats the mid signal over the live sides.
