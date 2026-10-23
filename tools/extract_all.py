#!/usr/bin/env python3
"""End-to-end Skate or Die (IIGS) extractor.
usage: python3 -I tools/extract_all.py <disk.2mg|.po> <outdir>
Writes: raw/ (ProDOS files), res/ (LZW-decompressed), png/ (screens + sprite sheets), manifest.txt"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prodos, sodlzw, sprites
from PIL import Image

img_path, outdir = sys.argv[1], sys.argv[2]
for sub in ('raw', 'res', 'png'): os.makedirs(f'{outdir}/{sub}', exist_ok=True)
d = prodos.load(img_path)
ents = []; prodos.walk(d, 2, '/', ents)
man = []
files = {}
for name, st, ft, kp, bu, eof, aux in ents:
    if st == 0xD: continue
    data = prodos.readfile(d, st, kp, eof)
    files[name] = data
    open(f"{outdir}/raw/{name.strip('/').replace('/', '_')}", 'wb').write(data)

def shr_image(data, pal_index=0):
    pix, scb, pal = data[:32000], data[32000:32200], data[32256:32768]
    im = Image.new('RGB', (320, 200)); px = im.load()
    for y in range(200):
        p = scb[y] & 15
        cols = []
        for i in range(16):
            v = pal[p*32 + i*2] | (pal[p*32 + i*2 + 1] << 8)
            cols.append((((v >> 8) & 15)*17, ((v >> 4) & 15)*17, (v & 15)*17))
        for x in range(160):
            b = pix[y*160 + x]; px[x*2, y] = cols[b >> 4]; px[x*2 + 1, y] = cols[b & 15]
    return im

res = {}
for name, data in files.items():
    m = re.fullmatch(r'/[AB]/F(\d+)', name)
    if not m: continue
    n = int(m.group(1))
    if len(data) > 5 and ((data[4] | (data[5] << 8)) & 0x1FF) == 0x1C8:
        w1, count, _, out = sodlzw.decompress(data)
        out = out[:w1]
        res[n] = out; open(f'{outdir}/res/F{n}.bin', 'wb').write(out)
        man.append(f'F{n}: lzw {len(data)} -> {len(out)} (hdr {w1}, codes {count})')
    else:
        res[n] = data; open(f'{outdir}/res/F{n}.bin', 'wb').write(data)
        man.append(f'F{n}: raw {len(data)}')

for n, data in res.items():
    if len(data) == 32768 and n not in (2,):
        shr_image(data).save(f'{outdir}/png/screen_F{n}.png')

# sprite sheets: (table, image, mask, palette source) -- palette association is provisional
for t, i, m, p in [(4,5,6,1),(12,13,14,9),(18,19,20,16),(25,26,27,22),(36,37,38,32)]:
    if t not in res or i not in res: continue
    tab = sprites.load_table(f'{outdir}/res/F{t}.bin')
    pal_src = res[p]
    cols = sprites.palette(pal_src) if len(pal_src) == 32768 else [(( (pal_src[k*2+1]&15)*17), ((pal_src[k*2]>>4)*17), ((pal_src[k*2]&15)*17)) for k in range(16)]
    S = sprites.sheet(tab, res[i], res[m], cols, cols_per_row=30, cell=(28, 32))
    S.resize((S.width*2, S.height*2), 0).save(f'{outdir}/png/sprites_F{i}_F{m}.png')
open(f'{outdir}/manifest.txt', 'w').write('\n'.join(man) + '\n')
print('done:', len(files), 'files,', len(res), 'resources')
