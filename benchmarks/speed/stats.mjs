// stats.mjs - token statistics of an AMYZ stream (by decoding it).
// bit: flag and gamma bits; offBit: bucket index and extra bits (end marker included);
// litTok/litByte: literal runs and bytes; repTok; newTok; matchByte: bytes copied by matches.
export function tokenStats(z, widths) {
    const bases = []; let b = 1;
    for (let k = 0; k < 15; k++) { bases.push(b); b += 2 ** widths[k]; }
    const st = { bit: 0, offBit: 0, litTok: 0, litByte: 0, repTok: 0, newTok: 0, matchByte: 0, out: 0 };
    let i = 0, mask = 0, cur = 0, field = 'bit';
    const bit = () => { if (mask === 0) { cur = z[i++]; mask = 0x80; } const r = cur & mask ? 1 : 0; mask >>= 1; st[field]++; return r; };
    const bits = (n) => { let v = 0; for (let k = 0; k < n; k++) v = v << 1 | bit(); return v; };
    const gamma = () => { let v = 1; while (!bit()) v = v << 1 | bit(); return v; };
    let state = 0;
    for (;;) {
        if (state === 0) {
            const n = gamma(); i += n; st.litTok++; st.litByte += n; st.out += n;
            state = bit() ? 2 : 1;
        } else {
            let n;
            if (state === 1) { n = gamma(); st.repTok++; }
            else {
                field = 'offBit'; const k = bits(4);
                if (k === 15) { field = 'bit'; return st; }
                bits(widths[k]); field = 'bit'; n = gamma() + 1; st.newTok++;
            }
            st.matchByte += n; st.out += n;
            state = bit() ? 2 : 0;
        }
    }
}
