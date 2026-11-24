#!/usr/bin/env python3
"""nes_convert.py - adapt the Warrior TMS9918A Graphics II bitmap to the NES.

The conversion:
  - the picture is placed 2 tile rows down (16 px), so the NES 16x16 attribute
    areas line up with 16x16 areas of the picture;
  - the 4 background palettes (3 colours each + black) are chosen by optimisation:
    minimise the weighted number of pixels that must be recoloured, where hair
    reds, skin and the face area weigh much more (greedy start, then local search
    over all colour triples until no swap improves);
  - pixels outside their area's palette get the nearest palette colour (counted);
  - optional sprite overlay (--sprites): 8x8 sprites with their own 4 palettes
    restore recoloured pixels in the face and hair, at most 8 per scanline and 64
    in all (no flicker: the limits are checked here).
Writes chr.bin (8192: table 0 then table 1), nt.bin (960 + 64), pal.bin (32: BG
then sprite palettes), oam.bin (256 B OAM image; sprite 0 = split marker), split.txt,
stats.txt and preview.png (the expected NES picture).
usage: nes_convert.py pattern.bin color.bin outdir [--sprites]
"""
import sys, itertools, collections

pat = open(sys.argv[1], 'rb').read(); col = open(sys.argv[2], 'rb').read(); out = sys.argv[3]
SPRITES = '--sprites' in sys.argv
ROW0 = 2                                     # first screen tile row of the picture
TMS2NES = {0: 0x0F, 1: 0x0F, 2: 0x1A, 3: 0x2A, 4: 0x12, 5: 0x22, 6: 0x06, 7: 0x2C,
           8: 0x16, 9: 0x26, 10: 0x28, 11: 0x38, 12: 0x0A, 13: 0x14, 14: 0x10, 15: 0x30}
# approximate NES palette RGB (2C02), for distances and the preview
NESRGB = {0x0F: (0, 0, 0), 0x1A: (0, 168, 0), 0x2A: (88, 216, 84), 0x02: (0, 0, 168), 0x12: (0, 88, 248), 0x22: (60, 188, 252),
          0x06: (168, 16, 0), 0x2C: (0, 232, 216), 0x16: (248, 56, 0), 0x26: (248, 120, 88), 0x28: (248, 184, 0),
          0x38: (248, 216, 120), 0x0A: (0, 120, 0), 0x14: (216, 0, 204), 0x10: (188, 188, 188), 0x30: (252, 252, 252)}
TMSRGB = {1: (0, 0, 0), 4: (84, 85, 237), 5: (125, 118, 252), 6: (212, 82, 77), 7: (66, 235, 245), 8: (252, 85, 84),
          10: (212, 193, 84), 11: (230, 206, 128), 13: (201, 91, 186), 14: (204, 204, 204), 15: (255, 255, 255)}

W, H = 256, 192
tms = [[1] * W for _ in range(H)]
for ty in range(24):
    for tx in range(32):
        t = ty * 32 + tx
        for r in range(8):
            p = pat[t * 8 + r]; c = col[t * 8 + r]
            for b in range(8):
                tms[ty * 8 + r][tx * 8 + b] = (c >> 4) if p & (0x80 >> b) else (c & 15)
img = [[TMS2NES[v] for v in row] for row in tms]

def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(NESRGB[a], NESRGB[b]))

# importance: hair reds and skin weigh most; the face area (around the head) too
FACE = (100, 64, 168, 112)                   # x0, y0, x1, y1 in picture pixels (head, hair, face)
def weight(x, y, c):
    w = {0x16: 20, 0x06: 20, 0x38: 12, 0x28: 12, 0x30: 4}.get(c, 1)
    if FACE[0] <= x < FACE[2] and FACE[1] <= y < FACE[3]:
        w *= 4
    return w

areas = {}                                   # (ay, ax) -> Counter colour -> weight, and pixel counts
for ay in range(12):
    for ax in range(16):
        wc = collections.Counter(); pc = collections.Counter()
        for y in range(ay * 16, ay * 16 + 16):
            for x in range(ax * 16, ax * 16 + 16):
                c = img[y][x]
                if c == 0x0F:
                    continue
                wc[c] += weight(x, y, c); pc[c] += 1
        areas[(ay, ax)] = (wc, pc)
colours = sorted({c for wc, _ in areas.values() for c in wc})
triples = [tuple(t) for t in itertools.combinations(colours, 3)]
# cost of an area under a palette: weighted pixels not in it (they must be recoloured)
cost = {k: {t: sum(w for c, w in wc.items() if c not in t) for t in triples} for k, (wc, _) in areas.items()}
def total(pals):
    return sum(min(cost[k][p] for p in pals) for k in areas)
pals = []
for _ in range(4):
    pals.append(min(triples, key=lambda t: total(pals + [t])))
improved = True
while improved:
    improved = False
    for i in range(4):
        for t in triples:
            trial = pals[:i] + [t] + pals[i + 1:]
            if total(trial) < total(pals):
                pals = trial; improved = True
apal = {k: min(range(4), key=lambda i: (cost[k][pals[i]], i)) for k in areas}

# recolour: nearest colour of the area's palette (or black)
nes = [[0] * W for _ in range(H)]
idx = [[0] * W for _ in range(H)]
changed = 0; changed_face = 0
for y in range(H):
    for x in range(W):
        c = img[y][x]; choices = [0x0F] + list(pals[apal[(y // 16, x // 16)]])
        i = min(range(4), key=lambda j: (0 if choices[j] == c else 1, dist(c, choices[j])))
        nes[y][x] = choices[i]; idx[y][x] = i
        if choices[i] != c:
            changed += 1
            if FACE[0] <= x < FACE[2] and FACE[1] <= y < FACE[3]:
                changed_face += 1

# optional sprite overlay: 8x8 cells in the face box with recoloured pixels get a
# sprite (own palettes) drawing the true colours on top
sprites = []; spals = []
if SPRITES:
    cells = []
    for cy in range(FACE[1] // 8, FACE[3] // 8):
        for cx in range(FACE[0] // 8, FACE[2] // 8):
            bad = [(x, y) for y in range(cy * 8, cy * 8 + 8) for x in range(cx * 8, cx * 8 + 8) if nes[y][x] != img[y][x]]
            if bad:
                cells.append((len(bad), cx, cy, bad))
    cells.sort(reverse=True)
    sprite_cols = collections.Counter()
    for _, cx, cy, bad in cells:
        for x, y in bad:
            sprite_cols[img[y][x]] += 1
    # sprite palettes: groups of up to 3 colours, most frequent first
    sc = [c for c, _ in sprite_cols.most_common(12)]
    spals = [sc[i:i + 3] for i in range(0, len(sc), 3)][:4]
    per_line = collections.Counter()
    for n, cx, cy, bad in cells:
        need = {img[y][x] for x, y in bad}
        p = next((i for i, sp in enumerate(spals) if need <= set(sp)), None)
        if p is None:
            p = max(range(len(spals)), key=lambda i: sum(1 for x, y in bad if img[y][x] in spals[i]))
        lines = range(cy * 8, cy * 8 + 8)
        if len(sprites) >= 63 or any(per_line[l] >= 7 for l in lines):   # sprite 0 is reserved
            continue
        for l in lines:
            per_line[l] += 1
        sprites.append((cx, cy, p, [(x, y) for x, y in bad if img[y][x] in spals[p]]))
    for _, _, p, px in sprites:
        for x, y in px:
            nes[y][x] = img[y][x]
    changed_after = sum(1 for y in range(H) for x in range(W) if nes[y][x] != img[y][x])
    face_after = sum(1 for y in range(FACE[1], FACE[3]) for x in range(FACE[0], FACE[2]) if nes[y][x] != img[y][x])
else:
    changed_after = changed; face_after = changed_face

def tile_bytes(cells):
    lo = []; hi = []
    for r in range(8):
        a = b = 0
        for x in range(8):
            i = cells[r][x]; a |= (i & 1) << (7 - x); b |= (i >> 1) << (7 - x)
        lo.append(a); hi.append(b)
    return bytes(lo + hi)

tables = [[bytes(16)], [bytes(16)]]
nt = bytearray(960)
for ty in range(24):
    half = 0 if ty < 12 else 1
    for tx in range(32):
        tb = tile_bytes([[idx[ty * 8 + r][tx * 8 + x] for x in range(8)] for r in range(8)])
        if tb not in tables[half]:
            tables[half].append(tb)
        nt[(ty + ROW0) * 32 + tx] = tables[half].index(tb)
# sprite tiles go after the background tiles of table 0 (sprites use table 0)
spr_tiles = []
for cx, cy, p, px in sprites:
    sp = spals[p]; cellv = [[0] * 8 for _ in range(8)]
    for x, y in px:
        cellv[y - cy * 8][x - cx * 8] = sp.index(img[y][x]) + 1
    tb = tile_bytes(cellv)
    if tb not in tables[0]:
        tables[0].append(tb)
    spr_tiles.append(tables[0].index(tb))
for i, tl in enumerate(tables):
    if len(tl) > 256:
        sys.exit(f'pattern table {i}: {len(tl)} tiles > 256')
chr_ = bytearray(8192)
for i, tl in enumerate(tables):
    for j, tb in enumerate(tl):
        chr_[i * 4096 + j * 16:i * 4096 + j * 16 + 16] = tb
attr = bytearray(64)
for ay in range(8):
    for ax in range(8):
        v = 0
        for q, (dy, dx) in enumerate(((0, 0), (0, 1), (1, 0), (1, 1))):
            sy = ay * 2 + dy - ROW0 // 2          # picture area row
            if 0 <= sy < 12:
                v |= apal[(sy, ax * 2 + dx)] << (2 * q)
        attr[ay * 8 + ax] = v
pal = bytearray()
for p in pals:
    pal += bytes([0x0F] + list(p))
spals_full = [list(sp) + [0x0F] * (3 - len(sp)) for sp in spals] + [[0x0F] * 3] * (4 - len(spals))
for sp in spals_full:
    pal += bytes([0x0F] + sp)
# split marker: sprite 0 must hit on the LAST line of the top half, as far right as
# possible, so the mid-frame switch to pattern table 1 does not cut the top half.
# Its tile is a single opaque pixel over an opaque background pixel on that line.
sy = ROW0 + 11
py = (sy - ROW0) * 8 + 7                     # picture line
hx = max(x for x in range(W - 8) if idx[py][x] != 0)
marker = [[0] * 8 for _ in range(8)]; marker[7][hx % 8] = 1
mt = tile_bytes(marker)
if mt not in tables[0]:
    tables[0].append(mt)
    chr_[(len(tables[0]) - 1) * 16:(len(tables[0]) - 1) * 16 + 16] = mt
sx = hx // 8
oam = bytearray([0xF8] * 256)
oam[0:4] = bytes([sy * 8 - 1, tables[0].index(mt), 0x20, sx * 8])
for n, ((cx, cy, p, px), t) in enumerate(zip(sprites, spr_tiles), start=1):
    oam[n * 4:n * 4 + 4] = bytes([(cy + ROW0) * 8 - 1, t, p, cx * 8])
open(out + '/chr.bin', 'wb').write(chr_)
open(out + '/nt.bin', 'wb').write(bytes(nt) + bytes(attr))
open(out + '/pal.bin', 'wb').write(pal)
open(out + '/oam.bin', 'wb').write(oam)
open(out + '/split.txt', 'w').write(f'sprite 0: tile row {sy}, column {sx}, hit on picture line {py} at x {hx}\n')
stats = (f'palettes {[[hex(c) for c in p] for p in pals]}\n'
         f'pixels recoloured to fit the attribute palettes: {changed} of {W * H} '
         f'({changed_face} in the face/hair box {FACE})\n'
         f'sprites: {len(sprites)} (+ sprite 0), sprite palettes {[[hex(c) for c in p] for p in spals]}, '
         f'pixels still different from the original after the sprites: {changed_after} ({face_after} in the face/hair box)\n'
         f'tiles: table 0 {len(tables[0])} (incl. {len(set(spr_tiles))} sprite tiles), table 1 {len(tables[1])}\n')
open(out + '/stats.txt', 'w').write(stats); print(stats, end='')
try:
    from PIL import Image
    im = Image.new('RGB', (W, H))
    im.putdata([NESRGB[nes[y][x]] for y in range(H) for x in range(W)])
    im.save(out + '/preview.png')
except ImportError:
    pass
