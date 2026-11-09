#!/usr/bin/env python3
"""Rebuild windows/res/tinyhand.ico and appicon.png from the macOS app icon (needs Pillow)."""

from pathlib import Path

from PIL import Image

root = Path(__file__).resolve().parents[2]
source = Image.open(root / "assets" / "AppIcon.icns")
icon = source.convert("RGBA")
sizes = [(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 128, 256)]
icon.save(Path(__file__).with_name("tinyhand.ico"), sizes=sizes)
icon.resize((256, 256), Image.LANCZOS).save(Path(__file__).with_name("appicon.png"))
print("wrote tinyhand.ico and appicon.png in", Path(__file__).parent)
