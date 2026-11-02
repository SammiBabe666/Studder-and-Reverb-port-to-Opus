"""Assemble release/: the two .opmod files, SHA256SUMS.txt, and the release ZIP holding them with the install note, the
documentation, the licences, the test results and the complete source (plugins, tests, tools, third_party). Fixed
timestamps and order, so the same files always give the same ZIP.

    python3 -I tools/make_release.py BUILD_OPUS_DIR      (the folder tools/build.sh wrote the .opmod files to)
Copyright (C) 2026 sammi. GPL-3.0-or-later with the OP-1 firmware permission."""
import hashlib
import os
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION = "1.1.0"
NAME = f"sammi-reverb-stutter-{VERSION}"
PLUGINS = ["sammi.reverb.opmod", "sammi.stutter.opmod"]
TOP = ["README.md", "LICENSE", "LICENSE.elements", "NOTICE", "NOTICE.kennel"]
TREES = ["docs", "results", "plugins", "tests", "tools", "third_party"]
STAMP = (2026, 11, 2, 0, 0, 0)


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "opus")
    rel = os.path.join(ROOT, "release")
    os.makedirs(rel, exist_ok=True)
    for p in PLUGINS:
        shutil.copyfile(os.path.join(src, p), os.path.join(rel, p))
    sums = "".join(f"{sha(os.path.join(rel, p))}  {p}\n" for p in PLUGINS)
    open(os.path.join(rel, "SHA256SUMS.txt"), "w", newline="\n").write(sums)

    entries = [(f"mods/{p}", os.path.join(rel, p)) for p in PLUGINS]
    inner = "".join(f"{sha(os.path.join(rel, p))}  mods/{p}\n" for p in PLUGINS)   # paths as the ZIP holds them
    entries.append(("SHA256SUMS.txt", inner.encode()))
    entries.append(("INSTALL.txt", os.path.join(ROOT, "docs", "INSTALL.txt")))
    entries += [(t, os.path.join(ROOT, t)) for t in TOP]
    for tree in TREES:
        base = os.path.join(ROOT, tree)
        for d, dirs, files in os.walk(base):
            dirs[:] = sorted(x for x in dirs if x not in ("__pycache__",))
            for f in sorted(files):
                if f.endswith(".gif"):              # the animated previews stay in the repository, not the ZIP
                    continue
                full = os.path.join(d, f)
                arc = os.path.relpath(full, ROOT).replace(os.sep, "/")
                entries.append(((("source/" if tree in ("plugins", "tests", "tools", "third_party") else "") + arc), full))
    out = os.path.join(rel, f"{NAME}.zip")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for arc, full in entries:
            info = zipfile.ZipInfo(f"{NAME}/{arc}", STAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o755 if isinstance(full, str) and full.endswith(".sh") else 0o644) << 16
            z.writestr(info, full if isinstance(full, bytes) else open(full, "rb").read())
    open(os.path.join(rel, "SHA256SUMS.txt"), "a", newline="\n").write(f"{sha(out)}  {NAME}.zip\n")
    print(open(os.path.join(rel, "SHA256SUMS.txt")).read(), end="")
    print(f"{out}: {len(entries)} files, {os.path.getsize(out)} bytes")


if __name__ == "__main__":
    main()
