// pack_nes.mjs - AMYZ-compress the NES Warrior data (chr.bin, nt.bin) with one
// fitted table; writes chr.amyz, nt.amyz, nes.tabz (15 B). Round trip checked.
// usage: node pack_nes.mjs datadir
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fitGroupAMYZ, decompressTable, setMaxOffset } from '../lib/amyz.js';
const dir = process.argv[2];
setMaxOffset(16383);
const datas = ['chr.bin', 'nt.bin'].map((f) => new Uint8Array(readFileSync(join(dir, f))));
const fit = fitGroupAMYZ(datas, { search: false });
['chr', 'nt'].forEach((n, i) => {
    const back = decompressTable(fit.streams[i], fit.table);
    if (back.length !== datas[i].length || back.some((v, k) => v !== datas[i][k])) throw new Error('round trip ' + n);
    writeFileSync(join(dir, n + '.amyz'), fit.streams[i]);
    console.log(`${n}: ${datas[i].length} -> ${fit.streams[i].length} B`);
});
writeFileSync(join(dir, 'nes.tabz'), fit.table);
