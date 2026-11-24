// pack_gb.mjs - AMYZ-compress the Game Boy Warrior data (tiles.bin, map.bin)
// with one fitted table; writes tiles.amyz, map.amyz, gb.tabz (15 B) and
// gb.tabzx (45 B: (1 - base) lo, hi, width per bucket). Round trip checked.
// usage: node pack_gb.mjs datadir
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fitGroupAMYZ, decompressTable, setMaxOffset } from '../lib/amyz.js';
const dir = process.argv[2];
setMaxOffset(16383);
const names = ['tiles', 'map'];
const datas = names.map((n) => new Uint8Array(readFileSync(join(dir, n + '.bin'))));
const fit = fitGroupAMYZ(datas, { search: false });
names.forEach((n, i) => {
    const back = decompressTable(fit.streams[i], fit.table);
    if (back.length !== datas[i].length || back.some((v, k) => v !== datas[i][k])) throw new Error('round trip ' + n);
    writeFileSync(join(dir, n + '.amyz'), fit.streams[i]);
    console.log(`${n}: ${datas[i].length} -> ${fit.streams[i].length} B`);
});
writeFileSync(join(dir, 'gb.tabz'), fit.table);
const x = new Uint8Array(45); let base = 1;
for (let k = 0; k < 15; k++) {
    const w = fit.table[k] - 1, v = (1 - base) & 0xffff;
    x[3 * k] = v & 255; x[3 * k + 1] = v >> 8; x[3 * k + 2] = w; base += 2 ** w;
}
writeFileSync(join(dir, 'gb.tabzx'), x);
