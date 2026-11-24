// fit_model.mjs - measure the per-token decode cost of the AMYZ fast decoders.
// For every corpus stream: count tokens, bits and copied bytes (token statistics),
// run the decoder in its emulator harness for the real cycle count, then fit
//   cycles = c0 + bit*B + litTok*LT + litByte*LB + repTok*RT + newTok*NT + matchByte*MB + offBit*OB
// by least squares. Writes model_6502.json and model_z80.json.
// usage: node fit_model.mjs STREAMS TABLES CORPUS WGAME [extra streams list.tsv]
import { readFileSync, writeFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { tokenStats } from './stats.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const [STREAMS, TABLES, CORPUS, WGAME] = process.argv.slice(2);
const rows = [];
for (const line of readFileSync(join(STREAMS, 'files.tsv'), 'utf8').trim().split('\n')) {
    const [cat, name, , game] = line.split('\t');
    const sp = join(STREAMS, cat, name);
    const op = cat === 'warrior' ? join(WGAME, 'warrior', name) : join(CORPUS, cat, name);
    const t15 = new Uint8Array(readFileSync(join(TABLES, game + '.tabz')));
    const st = tokenStats(new Uint8Array(readFileSync(sp)), Array.from(t15, (x) => x - 1));
    const r6 = spawnSync(join(here, 'cyc6502'), [join(here, 'amyzf_6502.bin'), join(TABLES, game + '.tabzx'), sp, op]).stdout.toString().trim().split(' ');
    const rz = spawnSync(join(here, 'z80', 'vtest'), [join(here, 'z80', 'amyzf.bin'), sp, op, '0', join(TABLES, game + '.tabzx')]).stdout.toString();
    if (r6[0] !== 'OK' || !rz.startsWith('OK')) throw new Error('decode failed ' + name);
    rows.push({ name: cat + '/' + name, st, c6502: +r6[1], cz80: +rz.match(/ T=(\d+)/)[1] });
}
const KEYS = ['one', 'bit', 'litTok', 'litByte', 'repTok', 'newTok', 'matchByte', 'offBit'];
function lsq(target) {
    const n = KEYS.length, A = Array.from({ length: n }, () => new Float64Array(n + 1));
    for (const r of rows) {
        const x = KEYS.map((k) => (k === 'one' ? 1 : r.st[k]));
        for (let i = 0; i < n; i++) { for (let j = 0; j < n; j++) A[i][j] += x[i] * x[j]; A[i][n] += x[i] * r[target]; }
    }
    for (let i = 0; i < n; i++) {                   // Gauss-Jordan
        let p = i; for (let r = i + 1; r < n; r++) if (Math.abs(A[r][i]) > Math.abs(A[p][i])) p = r;
        [A[i], A[p]] = [A[p], A[i]];
        for (let r = 0; r < n; r++) if (r !== i) { const f = A[r][i] / A[i][i]; for (let c = i; c <= n; c++) A[r][c] -= f * A[i][c]; }
    }
    const coef = {}; KEYS.forEach((k, i) => { coef[k] = A[i][n] / A[i][i]; });
    let worst = 0, sumAbs = 0, sum = 0;
    for (const r of rows) {
        const pred = KEYS.reduce((s, k) => s + coef[k] * (k === 'one' ? 1 : r.st[k]), 0);
        const e = Math.abs(pred - r[target]) / r[target]; worst = Math.max(worst, e); sumAbs += Math.abs(pred - r[target]); sum += r[target];
    }
    return { coef, files: rows.length, meanAbsErrorPct: 100 * sumAbs / sum, worstFilePct: 100 * worst };
}
for (const [t, f] of [['c6502', 'model_6502.json'], ['cz80', 'model_z80.json']]) {
    const m = lsq(t); writeFileSync(join(here, f), JSON.stringify(m, null, 1));
    console.log(f, JSON.stringify(m.coef, (k, v) => (typeof v === 'number' ? Math.round(v * 100) / 100 : v)), `mean err ${m.meanAbsErrorPct.toFixed(2)}%, worst file ${m.worstFilePct.toFixed(1)}%`);
}
writeFileSync(join(here, 'model_rows.json'), JSON.stringify(rows));
