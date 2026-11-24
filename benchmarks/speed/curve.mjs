// curve.mjs - size / decode-cycles curve of the speed-aware AMYZ encoder.
// For each -speed N, every file is re-parsed with the same table (the size-optimal one)
// and the cost model of the chosen decoder; each stream is then decoded by the
// UNCHANGED decoder in its harness (byte-exact check) for the real cycle count.
// usage: node curve.mjs cpu(6502|z80) set(corpus|c64) N1,N2,...  > result lines (TSV)
//   corpus: STREAMS TABLES CORPUS WGAME from the environment (files.tsv layout)
//   c64:    C64DIR (raw program bytes NAME.bin) and C64TAB (NAME.tabz), names in C64LIST
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { compressTable, setCostModel, setMaxOffset, expandTable } from './lib/amyz.js';
import { speedModel } from './speedmodel.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const [cpu, set, list] = process.argv.slice(2);
const Ns = list.split(',').map(Number);
const E = process.env;
const items = [];
if (set === 'corpus') {
    for (const line of readFileSync(join(E.STREAMS, 'files.tsv'), 'utf8').trim().split('\n')) {
        const [cat, name, , game] = line.split('\t');
        const op = cat === 'warrior' ? join(E.WGAME, 'warrior', name) : join(E.CORPUS, cat, name);
        items.push({ name: cat + '/' + name, orig: op, tab: join(E.TABLES, game + '.tabz') });
    }
} else {
    setMaxOffset(65535);
    for (const n of E.C64LIST.split(',')) items.push({ name: n, orig: join(E.C64DIR, n + '.bin'), tab: join(E.C64TAB, n + '.tabz') });
}
const tmp = join(here, 'tmp'); mkdirSync(tmp, { recursive: true });
for (const N of Ns) {
    setCostModel(speedModel(cpu, N));
    let size = 0, cyc = 0, bytes = 0;
    for (const it of items) {
        const data = new Uint8Array(readFileSync(it.orig)), t15 = new Uint8Array(readFileSync(it.tab));
        const z = compressTable(data, t15);
        const zp = join(tmp, 's.bin'), tp = join(tmp, 't.bin');
        writeFileSync(zp, z); writeFileSync(tp, expandTable(t15));
        let r;
        if (cpu === '6502') {
            r = spawnSync(join(here, 'cyc6502'), [join(here, 'amyzf_6502.bin'), tp, zp, it.orig]).stdout.toString().trim().split(' ');
            if (r[0] !== 'OK') throw new Error(`decode FAIL ${it.name} N=${N}`);
            cyc += +r[1];
        } else {
            r = spawnSync(join(here, 'z80', 'vtest'), [join(here, 'z80', 'amyzf.bin'), zp, it.orig, '0', tp]).stdout.toString();
            if (!r.startsWith('OK')) throw new Error(`decode FAIL ${it.name} N=${N}`);
            if (!/gaps_under_29=0 /.test(r)) throw new Error('VRAM timing violation ' + it.name);
            cyc += +r.match(/ T=(\d+)/)[1];
        }
        size += z.length; bytes += data.length;
    }
    console.log(`${cpu}\t${set}\t${N}\t${size}\t${cyc}\t${(cyc / bytes).toFixed(2)}\t${items.length} files OK`);
}
