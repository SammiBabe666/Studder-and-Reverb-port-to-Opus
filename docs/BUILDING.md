# Building and testing

You don't need any of this to use the plugins: `release/` has them ready.

## What you need

- **Python 3.8+**, a **host C compiler** with AddressSanitizer (gcc or clang) and **numpy** (for `analysis.py`).
- **The Opus plugin kit:** `git clone https://github.com/LizardMan245/opus` → `opus/plugin-kit` (`OPUS_KIT`).
- **Hundekraft's source** (for the tests: the originals the ports are compared against, and op1r's mocks):
  ```sh
  git clone --depth 1 https://github.com/Hundekraft/op1-workshop
  cd op1-workshop/source && sha256sum -c hundekraft-3.1.1-source.tar.gz.sha256 && tar xzf hundekraft-3.1.1-source.tar.gz
  # KENNEL=$PWD/hundekraft-3.1.1-source/ext/kennel
  ```
- **The Blackfin compiler** (`bfin-elf-gcc` 13.3.0 + binutils). Either the kit's own recipe, if you can reach
  ftp.gnu.org:
  ```sh
  WORK=$HOME/bfin-build bash $OPUS_KIT/tools/toolchain/build-bfin-toolchain.sh fetch verify extract linux
  ```
  or, from git sources only (what was used here; about an hour on two cores):
  ```sh
  sh tools/toolchain/build_toolchain.sh $HOME/bfin-tc        # -> $HOME/bfin-tc/bfin/bin
  sh tools/toolchain/build_sim.sh $HOME/bfin-tc              # the simulator -> $HOME/bfin-tc/build/sim/sim/bfin/run
  ```

## Commands

```sh
export PATH=$HOME/bfin-tc/bfin/bin:$PATH OPUS_KIT=/path/opus/plugin-kit KENNEL=/path/hundekraft-3.1.1-source/ext/kennel
export BFIN_RUN=$HOME/bfin-tc/build/sim/sim/bfin/run

sh tools/build.sh                 # the two .opmod files -> build/opus/, read back with plugin_tool.py show
sh tests/host/run.sh              # host tests (ASan + UBSan): differential, port checks, analysis
sh tests/bfin_sim/run.sh          # the compiled code in the simulator: against the originals, cycles
sh tools/previews.sh              # the pages' previews -> docs/media (needs Pillow)
sh tools/release.sh               # all of the above, results/ refreshed, release/ assembled (make_release.py)
```

`release.sh` writes `release/sammi.reverb.opmod`, `release/sammi.stutter.opmod`, `release/SHA256SUMS.txt` and
`release/sammi-reverb-stutter-1.1.0.zip` (fixed timestamps: the same files give the same ZIP).

## Layout

```
plugins/sammi.reverb/      reverb.c (the port), manifest.txt, README.md
plugins/sammi.stutter/     stutter.c (the port), manifest.txt, README.md
third_party/               Hundekraft's original reverb.c, stutter.c and plugin.json files, unchanged
tests/host/                scenario.h (shared scenarios), orig_runner.c, port_runner.c, port_checks.c, analysis.py,
                           opus_mock.h, harness/op1_host.h (op1r's mocks), run.sh
tests/bfin_sim/            bfin_runner.c, start.S, sim.ld, run.sh
tools/                     build.sh, release.sh, make_release.py, toolchain/ (build_toolchain.sh, build_sim.sh)
docs/                      CONTROLS.md, TESTING.md, MODIFICATIONS.md, BUILDING.md, INSTALL.txt, diffs/
results/                   the logs of the last tools/release.sh
release/                   the files to install, their checksums and the ZIP
```

## After measuring the CPU on an OP-1

Write the figure into `plugins/sammi.X/cpu.txt`:

```
cycles N
measured YYYY-MM-DD on my OP-1: what was played
```

Then add a `checked device YYYY-MM-DD what was played` line to its `manifest.txt`, raise its `version`, and run
`tools/release.sh` again. Remember to update the `VERSION` in `tools/make_release.py` too.
