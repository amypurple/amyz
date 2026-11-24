#!/usr/bin/env python3
"""gb_convert.py - convert the Warrior TMS9918A Graphics II bitmap (256x192,
15 colours) to a Game Boy picture in 4 greys.

The 256x192 picture is resampled to 160x120 (box filter on luminance, so the
whole picture stays visible), quantised to 4 grey levels (1-D k-means on the
resampled luminance, no dithering, so that tiles repeat), cut into 8x8 tiles,
2 bpp, and identical tiles are merged.

The DMG background can address 256 tiles at a time (LCDC.4 = 1: $8000-$8FFF).
After merging, Warrior needs 186 tiles, so no split across tile blocks and no
window are needed; the script stops with an error if a picture needs more
than 256 (a mid-frame LCDC.4 switch would be needed; not implemented).

Writes tiles.bin (16 B per unique tile, loaded at $8000), map.bin (32x18
tile map rows, 576 B, loaded at $9800), preview.png (the expected screen,
160x144, shown 4x, shade 0..3 = white..black), stats.txt.
usage: gb_convert.py pattern.bin color.bin outdir
"""
import sys, os
from PIL import Image

pat = open(sys.argv[1], 'rb').read(); col = open(sys.argv[2], 'rb').read(); out = sys.argv[3]
os.makedirs(out, exist_ok=True)
TMSRGB = {0: (0, 0, 0), 1: (0, 0, 0), 2: (33, 200, 66), 3: (94, 220, 120), 4: (84, 85, 237), 5: (125, 118, 252),
          6: (212, 82, 77), 7: (66, 235, 245), 8: (252, 85, 84), 9: (255, 121, 120), 10: (212, 193, 84),
          11: (230, 206, 128), 12: (33, 176, 59), 13: (201, 91, 186), 14: (204, 204, 204), 15: (255, 255, 255)}
W, H = 256, 192
lum = [[0.0] * W for _ in range(H)]
for ty in range(24):
    for tx in range(32):
        t = ty * 32 + tx
        for r in range(8):
            p = pat[t * 8 + r]; c = col[t * 8 + r]
            for b in range(8):
                v = (c >> 4) if p & (0x80 >> b) else (c & 15)
                R, G, B = TMSRGB[v]
                lum[ty * 8 + r][tx * 8 + b] = 0.299 * R + 0.587 * G + 0.114 * B
PW, PH = 160, 120
sx, sy = W / PW, H / PH
small = []
for y in range(PH):
    row = []
    for x in range(PW):
        x0, x1, y0, y1 = x * sx, (x + 1) * sx, y * sy, (y + 1) * sy
        acc = wsum = 0.0
        for yy in range(int(y0), min(H, int(y1 + 0.999))):
            wy = min(y1, yy + 1) - max(y0, yy)
            for xx in range(int(x0), min(W, int(x1 + 0.999))):
                wx = min(x1, xx + 1) - max(x0, xx)
                w = wx * wy
                if w > 0:
                    acc += w * lum[yy][xx]; wsum += w
        row.append(acc / wsum)
    small.append(row)
vals = sorted(v for r in small for v in r)
cent = [vals[int(len(vals) * q)] for q in (0.1, 0.4, 0.7, 0.95)]
for _ in range(50):
    groups = [[] for _ in range(4)]
    for v in vals:
        groups[min(range(4), key=lambda i: abs(v - cent[i]))].append(v)
    cent = [sum(g) / len(g) if g else cent[i] for i, g in enumerate(groups)]
    cent.sort()
# GB shade: 0 = white ... 3 = black; brightest cluster -> 0
q = [[3 - min(range(4), key=lambda i: abs(v - cent[i])) for v in r] for r in small]
PIC_ROW0 = 1                         # tile row of the picture's top on the map
TR, TC = PH // 8, PW // 8            # 15 x 20 tiles
def tile_bytes(ty, tx):
    b = bytearray()
    for r in range(8):
        lo = hi = 0
        for c in range(8):
            s = q[ty * 8 + r][tx * 8 + c]
            lo |= (s & 1) << (7 - c); hi |= (s >> 1) << (7 - c)
        b += bytes((lo, hi))
    return bytes(b)
tiles = [[tile_bytes(ty, tx) for tx in range(TC)] for ty in range(TR)]
blank = bytes([0xFF] * 16)              # border: shade 3 (black), like the picture's background
uniq = sorted(set(t for r in tiles for t in r) | {blank}, key=lambda t: t != blank)
if len(uniq) > 256:
    # would need a mid-frame LCDC.4 switch to reach a second tile block (not implemented: not needed here)
    sys.exit(f'{len(uniq)} unique tiles: more than 256')
slot = {t: i for i, t in enumerate(uniq)}          # LCDC.4 = 1: tiles at $8000 + 16 * index
vram = bytearray(len(uniq) * 16)
for t, i in slot.items():
    vram[i * 16:(i + 1) * 16] = t
mp = bytearray(32 * 18)                            # rows 0..17 of the 32x32 BG map, unseen columns blank too
for ty in range(TR):
    for tx in range(TC):
        mp[(PIC_ROW0 + ty) * 32 + tx] = slot[tiles[ty][tx]]
open(os.path.join(out, 'tiles.bin'), 'wb').write(vram)
open(os.path.join(out, 'map.bin'), 'wb').write(mp)
# preview: decode from VRAM + map exactly like the hardware does
SH = [(224, 248, 208), (136, 192, 112), (52, 104, 86), (8, 24, 32)]
SH = [(255, 255, 255), (170, 170, 170), (85, 85, 85), (0, 0, 0)]
img = Image.new('RGB', (160, 144))
for y in range(144):
    for x in range(160):
        i = mp[(y // 8) * 32 + x // 8]
        sl = i
        lo = vram[sl * 16 + (y % 8) * 2]; hi = vram[sl * 16 + (y % 8) * 2 + 1]
        b = 7 - x % 8
        s = ((lo >> b) & 1) | (((hi >> b) & 1) << 1)
        img.putpixel((x, y), SH[s])
img.resize((640, 576), Image.NEAREST).save(os.path.join(out, 'preview.png'))
st = (f"picture 160x120 at map row {PIC_ROW0}; grey centres {[round(c) for c in cent]}\n"
      f"unique tiles incl. the black border tile: {len(uniq)} (of 300 picture tiles)\n")
open(os.path.join(out, 'stats.txt'), 'w').write(st)
print(st, end='')
