# sammi.stutter (`hkstutter`)

Hundekraft's STUTTER (OP-1 Workshop 3.1.1), a beat-locked stutter, as a native Opus 2 effect type for the synth, the
drum and the master. **Not yet run on an OP-1.**

| Encoder | Turn | Shift + turn |
|---|---|---|
| blue | rate: 1/4, 1/8, 1/8t, 1/16, 1/16t, 1/32 | REPEATS: 2, 3, 4, 6, 8, 16 parts (saved with the sound) |
| green | mode: repeat, brake, reverse, gate | (the OS's fine step) |
| white | shape: 2–12 ms fades; in gate, the open time | (the OS's fine step) |
| orange | chance: none to all | FREEZE: clockwise on, anticlockwise off (per position, not saved) |

- **Controls taken:** shift + blue and shift + orange, on its own effect page only (`modal`, never clashing).
- **Settings:** none.
- **Core:** 2.2.
- **Page:** the defective tape head ([../../docs/VISUALS.md](../../docs/VISUALS.md)); dimmed by the OS's own off overlay when switched off.
- **Deep, read-only:** the metronome's beat clock (`0x01AADABC`) and the tape's speed and position
  (`0xFF802970` / `74`), at firmware 246's addresses, as kennel reads them. Opus 2 has no tempo service.
- **CPU:** unmeasured. 12.7k cycles worst per block in the simulator (15.5k on the master); a 32.9k-cycle ring clear
  at init.
- **Memory:** 131,072 bytes of bulk per instance.

More: [../../README.md](../../README.md), [../../docs/CONTROLS.md](../../docs/CONTROLS.md),
[../../docs/TESTING.md](../../docs/TESTING.md).

```sh
python3 $OPUS_KIT/plugin_tool.py build plugins/sammi.stutter --out build/opus/sammi.stutter.opmod
```
