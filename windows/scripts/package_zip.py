#!/usr/bin/env python3
from pathlib import Path
import sys
import zipfile

repo = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
build = repo / "windows" / "build"
out.parent.mkdir(parents=True, exist_ok=True)
files = {
    build / "TinyHand.exe": "TinyHand.exe",
    build / "TinyHandUpdater.exe": "TinyHandUpdater.exe",
    repo / "LICENSE": "LICENSE.txt",
    repo / "VERSION": "VERSION",
}
for asset in sorted((repo / "assets").glob("hand_*.png")):
    files[asset] = f"assets/{asset.name}"
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for path, name in files.items():
        if not path.is_file():
            raise SystemExit(f"missing {path}")
        z.write(path, name)
with zipfile.ZipFile(out) as z:
    assert z.testzip() is None
    names = z.namelist()
    assert len(names) == len(files) and set(names) == set(files.values())
print(f"Created {out} with {len(files)} files")
