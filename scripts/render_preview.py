"""Screenshots of ModActivator's panel without the game, for the Nexus page and
the README: one PNG per tab, rendered from the menu's real Dear ImGui output.

  python scripts/render_preview.py --toolchain C:\\Tools\\llvm-mingw-...

Needs build/ModActivator-tests.asi (scripts/build.py). Writes
dist/screenshots/<tab>.png. The sample mods and texts are set in
ModActivatorPreview (src/activator.cpp).
"""
from pathlib import Path
import argparse
import ctypes
import os
import struct
import subprocess
import zlib

base = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--toolchain', default=os.environ.get('LLVM_MINGW'))
args = parser.parse_args()
if not args.toolchain:
    parser.error('Pass --toolchain PATH or set LLVM_MINGW to your extracted LLVM-MinGW folder')
tc = Path(args.toolchain).resolve() / 'bin'
build = base / 'build'
out = base / 'dist/screenshots'
out.mkdir(parents=True, exist_ok=True)

renderer = build / 'render_preview.exe'
subprocess.run([str(tc / 'x86_64-w64-mingw32-clang++.exe'), '-O2', '-std=c++17', '-static',
                '-o', str(renderer), str(base / 'scripts/render_preview.cpp')], check=True)

WIDTH, HEIGHT, MARGIN = 1000, 860, 24  # ModActivatorPreview's display size


def png(path, width, height, rows):
    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data +
                struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff))
    raw = b''.join(b'\x00' + row for row in rows)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' +
                     chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


m = ctypes.WinDLL(str(build / 'ModActivator-tests.asi'))
preview = m.ModActivatorPreview
preview.argtypes = [ctypes.c_char_p, ctypes.c_int]
for tab, name in [(0, 'mod-menus'), (1, 'toggles'), (2, 'hotkeys'), (65536, 'settings')]:
    data, pixels = build / f'preview-{name}.bin', build / f'preview-{name}.rgba'
    result = preview(str(data).encode(), tab)
    assert result == 0, f'preview of {name} failed with {result}'
    box = subprocess.run([str(renderer), str(data), str(pixels), str(WIDTH), str(HEIGHT)],
                         check=True, capture_output=True, text=True).stdout.split()
    x0, y0, x1, y1 = (int(v) for v in box)
    x0, y0 = max(0, x0 - MARGIN), max(0, y0 - MARGIN)
    x1, y1 = min(WIDTH, x1 + MARGIN), min(HEIGHT, y1 + MARGIN)
    rgba = pixels.read_bytes()
    rows = [rgba[(y * WIDTH + x0) * 4:(y * WIDTH + x1) * 4] for y in range(y0, y1)]
    png(out / f'{name}.png', x1 - x0, y1 - y0, rows)
    print(f'{out / name}.png  {x1 - x0}x{y1 - y0}')

# The Nexus thumbnail: the whole 1280 x 720 scene, not cropped.
thumb = m.ModActivatorThumbnail
thumb.argtypes = [ctypes.c_char_p, ctypes.c_int]
data, pixels = build / 'preview-thumbnail.bin', build / 'preview-thumbnail.rgba'
result = thumb(str(data).encode(), 0)
assert result == 0, f'thumbnail failed with {result}'
subprocess.run([str(renderer), str(data), str(pixels), '1280', '720'], check=True,
               capture_output=True)
rgba = pixels.read_bytes()
png(out / 'thumbnail.png', 1280, 720, [rgba[y * 1280 * 4:(y + 1) * 1280 * 4] for y in range(720)])
print(f'{out / "thumbnail.png"}  1280x720')
