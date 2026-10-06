#!/usr/bin/env python3
"""Собирает sd-card.zip — содержимое SD-карты платы (boot.lua, библиотеки Lua,
utils/ESPLoader.bin) из examples/BoardH723/app/SD.

Архив распаковывают в корень карты FAT32. Использование:
    python tools/make_sd_zip.py [выходной.zip]   # по умолчанию build/sd-card.zip
"""
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "examples" / "BoardH723" / "app" / "SD"


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "sd-card.zip"
    out.parent.mkdir(parents=True, exist_ok=True)
    files = sorted(p for p in SRC.rglob("*") if p.is_file())
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for p in files:
            z.write(p, p.relative_to(SRC).as_posix())
    print(f"{out}: {len(files)} файлов, {out.stat().st_size // 1024} КБ")


if __name__ == "__main__":
    main()
