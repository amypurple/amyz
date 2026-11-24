// speedmodel.mjs - the -speed N cost model: cost = 1000 * bits + N * decoder cycles,
// with the per-token cycle costs measured by fit_model.mjs (model_6502.json, model_z80.json).
// -speed 0 is the plain size-optimal parse. N = 1000 values one cycle like one bit; N = 1 trades 1 bit for 1000 cycles.
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
export function speedModel(cpu, N) {
    if (!N) return null;
    const c = JSON.parse(readFileSync(join(here, cpu === 'z80' ? 'model_z80.json' : 'model_6502.json'), 'utf8')).coef;
    const r = Math.round;
    return {
        bit: r(1000 + N * c.bit), litByte: r(8000 + N * c.litByte), offBit: r(1000 + N * c.offBit),
        litTok: r(N * c.litTok), repTok: r(N * c.repTok), newTok: r(N * c.newTok), matchByte: r(N * c.matchByte),
    };
}
