# sammi.reverb (`hkreverb`)

Hundekraft's Reverb (OP-1 Workshop 3.1.1), the Mutable Instruments Elements tank in fixed point, as a native Opus 2
effect type for the synth, the drum and the master. **Not yet run on an OP-1.**

| Encoder | Turn |
|---|---|
| blue | time: decay, loop gain 0.35 to 0.98 |
| green | tone: the loop's low-pass, dark to bright |
| white | diffusion: allpass gain 0.5 to 0.75 |
| orange | mix: dry to wet |

- **Controls taken:** none outside its own effect page; shift turns are the OS's fine steps.
- **Settings:** none.
- **Core:** 2.2.
- **Page:** the endless mirror ([../../docs/VISUALS.md](../../docs/VISUALS.md)); dimmed by the OS's own off overlay when switched off.
- **CPU:** unmeasured. 14.7k cycles worst per block in the Blackfin simulator (17.5k on the master); about 25–30k
  estimated on a device.
- **Memory:** 86,652 bytes of bulk per instance.

More: [../../README.md](../../README.md), [../../docs/CONTROLS.md](../../docs/CONTROLS.md),
[../../docs/TESTING.md](../../docs/TESTING.md).

```sh
python3 $OPUS_KIT/plugin_tool.py build plugins/sammi.reverb --out build/opus/sammi.reverb.opmod
```
