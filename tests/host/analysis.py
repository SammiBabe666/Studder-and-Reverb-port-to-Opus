"""What the reverb's sound does, measured on port_checks' renders (tests/host/run.sh writes them):
decay (RT60 from the impulse responses' Schroeder curves) against time, the tail's brightness against tone, echo
density against diffusion, the dry and wet shares against mix, and the master keeping the dry stereo image.
The stutter's behaviour is checked sample by sample in port_checks.c (grid, slice and part lengths, every mode).

Times assume 44.1 kHz at the effect's input; kennel's notes say the OS's mono effect path may run at a lower rate
after R246's decimator, which would make every time here longer on the device by the same factor (the port and the
original run on the same path, so they match each other either way).

    python3 -I tests/host/analysis.py BUILD_DIR      -> prints a table, writes BUILD_DIR/analysis.txt, exit 1 on failure
Copyright (C) 2026 sammi. GPL-3.0-or-later with the OP-1 firmware permission."""
import sys
import numpy as np

SR = 44100.0
d = sys.argv[1] if len(sys.argv) > 1 else "."
lines, fails = [], []


def load(name):
    return np.fromfile(f"{d}/{name}", dtype=np.int16).astype(np.float64)


def rt60(ir):
    e = ir ** 2
    edc = np.cumsum(e[::-1])[::-1]
    edc = 10 * np.log10(np.maximum(edc / edc[0], 1e-30))
    i5, i35 = np.argmax(edc < -5), np.argmax(edc < -35)       # T30 from -5 to -35 dB, doubled
    if i35 <= i5:
        return float("inf")
    return 2.0 * (i35 - i5) / SR


def hf_ratio(ir):
    tail = ir[int(0.05 * SR):int(0.8 * SR)]
    sp = np.abs(np.fft.rfft(tail * np.hanning(len(tail)))) ** 2
    f = np.fft.rfftfreq(len(tail), 1 / SR)
    return 10 * np.log10(sp[f > 3000].sum() / max(sp[(f > 100) & (f < 1000)].sum(), 1e-30))


def density(ir):
    seg = ir[int(0.005 * SR):int(0.08 * SR)]
    return float(np.mean(np.abs(seg) > 0.1 * np.max(np.abs(seg))))


def check(ok, what):
    (lines if ok else fails).append(("PASS " if ok else "FAIL ") + what)


# decay and tone
rt = np.zeros((3, 3)); hf = np.zeros((3, 3))
for t in range(3):
    for k in range(3):
        ir = load(f"ir_t{t}_k{k}.raw")
        rt[t, k] = rt60(ir); hf[t, k] = hf_ratio(ir)
lines.append("RT60 (s) by time 0 / 16000 / 32767 (rows) x tone 0 / 20000 / 32767 (columns), mix all wet:")
for t in range(3):
    lines.append("   " + "  ".join(f"{rt[t, k]:7.2f}" for k in range(3)))
lines.append("tail brightness (dB, 3 kHz+ against 100 Hz-1 kHz):")
for t in range(3):
    lines.append("   " + "  ".join(f"{hf[t, k]:7.1f}" for k in range(3)))
for k in range(3):
    check(rt[0, k] < rt[1, k] < rt[2, k], f"RT60 rises with time at tone column {k}: {rt[0, k]:.2f} < {rt[1, k]:.2f} < {rt[2, k]:.2f} s")
for t in range(3):
    check(hf[t, 0] < hf[t, 1] < hf[t, 2], f"the tail brightens with tone at time row {t}: {hf[t, 0]:.1f} < {hf[t, 1]:.1f} < {hf[t, 2]:.1f} dB")
d0, d1 = density(load("ir_diff0.raw")), density(load("ir_diff1.raw"))
check(d1 >= d0, f"echo density with diffusion 0 -> max: {d0:.3f} -> {d1:.3f}")

# wet / dry
x = load("mix_in.raw")
shares = []
for m in range(4):
    y = load(f"mix_{m}.raw")
    g = float(np.dot(y, x) / np.dot(x, x))                   # the dry share: y's projection on x
    wet = y - g * x
    shares.append((g, 10 * np.log10(max(np.mean(wet ** 2), 1e-30) / np.mean(x ** 2))))
lines.append("mix 0 / 8192 / 16384 / 32767: dry gain, wet level (dB re input): " +
             ", ".join(f"{g:.3f} / {w:.1f}" for g, w in shares))
check(abs(shares[0][0] - 1) < 1e-9 and shares[0][1] < -200, "mix 0: the dry input alone")
check(all(shares[i][0] > shares[i + 1][0] for i in range(3)), "the dry share falls with mix")
check(all(shares[i][1] < shares[i + 1][1] for i in range(3)), "the wet share rises with mix")

# the master: the wet centred, the dry difference kept
l, r = load("master_l.raw"), load("master_r.raw")
diff = l - r
check(np.max(np.abs(diff - x)) == 0, "master: L - R out == L - R in (the dry image kept, the wet centred)")
check(np.max(np.abs(l)) <= 32767 and np.max(np.abs(r)) <= 32768, "master: within 16 bits")

out = "\n".join(lines + fails)
print(out)
open(f"{d}/analysis.txt", "w").write(out + "\n")
print(("FAIL" if fails else "PASS") + f" analysis: {len([s for s in lines if s.startswith('PASS')])} passed, {len(fails)} failed")
sys.exit(1 if fails else 0)
