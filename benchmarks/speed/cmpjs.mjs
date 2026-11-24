// compare C (-speed via AMYZ_SPEED) streams with the JS encoder for the same table and N
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { compressTable, setCostModel } from './lib/amyz.js';
import { speedModel } from './speedmodel.mjs';
const [cdir, N, STREAMS, TABLES, CORPUS, WGAME] = process.argv.slice(2);
setCostModel(speedModel('6502', +N));
let same = 0, diff = 0, size = 0;
for (const line of readFileSync(join(STREAMS, 'files.tsv'), 'utf8').trim().split('\n')) {
    const [cat, name, , game] = line.split('\t');
    const op = cat === 'warrior' ? join(WGAME, 'warrior', name) : join(CORPUS, cat, name);
    const z = compressTable(new Uint8Array(readFileSync(op)), new Uint8Array(readFileSync(join(TABLES, game + '.tabz'))));
    const c = new Uint8Array(readFileSync(join(cdir, cat, name)));
    if (Buffer.compare(Buffer.from(z), Buffer.from(c)) === 0) same++; else diff++;
    size += c.length;
}
console.log(`N=${N}: C and JS identical ${same}, different ${diff}, C total ${size}`);
