#!/usr/bin/env node
// amyzpack4 - small AMYZ self-extracting packer for the Commodore 64.
//
//   node amyzpack4.mjs [options] file.prg [file2.prg | raw.bin@0x2000 ...]
//     -o out.prg     output file (default packed.prg)
//     -x ADDR        start address after depacking ($080d, 0x080d or 2061);
//                    "basic" = BASIC RUN. Default: for a file at $0801 whose BASIC
//                    part is one "SYS nnnn" line, jmp nnnn; other files at $0801:
//                    BASIC RUN; otherwise the first file's load address.
//     -mode M        tiny | table | best (default best: packs both, keeps the smaller)
//     -top ADDR      force the end of the moved data (default: try $d000, $a000, $fff0)
//     -search        table search in table mode (slower, a few bytes smaller)
//     -v             print every layout tried
//     -tabz FILE     table mode with this 15-byte table (e.g. from the C encoder's search)
//     -speed N       speed-aware parse (0 = smallest; see speedmodel.mjs): cost = 1000 * bits + N * cycles
//     -dir D         fwd | bwd | best (default best): forward = data moved to high RAM and
//                    decrunched forward (amyzpack3); backward = decrunched in place from
//                    the end, no data copy (only the depacker moves to $0100/$0334)
//     -force         write the packed file even when it is not smaller than the input
//
// The .prg: "0 SYS2061" (12 B), stage 1 (30 B: sei, $01 = $34, page copy of the
// packed stream + depacker to high RAM, jmp), then the data. The depacker
// (depack.s) decodes each block forward, then runs the final bytes:
// lda #$37 / sta $01 [/ set $2d,$2e] / cli / jmp start (or jsr $a659 / jmp $a7ae).
// Those run in place when the depacker sits in RAM that stays RAM with $01 = $37
// (below $a000, or $c000-$cfff); otherwise they are copied to $0100 first.
// The packer replays the depacking and refuses any layout where an output byte
// would land on the depacker or on a stream byte not yet read.
// Zero page at run time: $57-$5f (BASIC float scratch). Stage 1 uses none.

import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { speedModel as speedModelSync } from './speedmodel.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const DEPACK = JSON.parse(readFileSync(join(here, 'depack.json'), 'utf8'));
const LOG2 = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14];

const parseAddr = (s) => {
    s = String(s).trim().toLowerCase();
    if (s.startsWith('$')) return parseInt(s.slice(1), 16);
    if (s.startsWith('0x')) return parseInt(s.slice(2), 16);
    return parseInt(s, 10);
};
const hex4 = (v) => '$' + v.toString(16).padStart(4, '0');

// decode one stream; used[i] = stream bytes consumed when output byte i is written
function decodeTrace(z, widths) {
    const bases = []; let b = 1;
    for (let k = 0; k < 15; k++) { bases.push(b); b += 2 ** widths[k]; }
    let i = 0, mask = 0, cur = 0; const out = []; const used = [];
    const bit = () => { if (mask === 0) { cur = z[i++]; mask = 0x80; } const r = cur & mask ? 1 : 0; mask >>= 1; return r; };
    const bits = (n) => { let v = 0; for (let k = 0; k < n; k++) v = v << 1 | bit(); return v; };
    const gamma = () => { let v = 1; while (!bit()) v = v << 1 | bit(); return v; };
    let last = 1, state = 0;
    for (;;) {
        if (state === 0) {
            const n = gamma();
            for (let k = 0; k < n; k++) { const v = z[i++]; used.push(i); out.push(v); }
            state = bit() ? 2 : 1;
        } else {
            let n;
            if (state === 1) n = gamma();
            else {
                const k = bits(4); if (k === 15) return { out: Uint8Array.from(out), used, len: i };
                last = bases[k] + bits(widths[k]); n = gamma() + 1;
            }
            for (let k = 0; k < n; k++) { used.push(i); out.push(out[out.length - last]); }
            state = bit() ? 2 : 0;
        }
    }
}

// final bytes: ROMs back, optional BASIC end pointer, cli, start
function finalBytes(mode, exec, basicEnd, noio) {
    const f = noio ? [] : [0xa9, 0x37, 0x85, 0x01];
    if (mode) f.push(0xa9, basicEnd & 0xff, 0x85, 0x2d, 0xa9, basicEnd >> 8, 0x85, 0x2e);
    if (!noio) f.push(0x58);
    if (mode) f.push(0x20, 0x59, 0xa6, 0x4c, 0xae, 0xa7); else f.push(0x4c, exec & 0xff, exec >> 8);
    return f;
}

const inRam37 = (a, e) => e <= 0xa000 || (a >= 0xc000 && e <= 0xd000);   // [a, e) stays RAM with $01 = $37

// build one packed file for a given encoding (streams + table) and depacker variant
function layout(blocks, enc, variant, top, fin, verbose, noio) {
    const d = DEPACK[variant];
    const img0 = Uint8Array.from(Buffer.from(d.image, 'hex'));
    const imgLen = img0.length + fin.length;
    // stream: per block hi, lo, data; then 0
    const ord = enc.order;
    const parts = []; let slen = 0;
    for (const i of ord) { parts.push({ i, hdr: slen, data: slen + 2 }); slen += 2 + enc.streams[i].length; }
    const single = ord.length === 1;
    if (!single) slen += 1;               // list terminator (dst hi = 0)
    const imgStart = Math.floor((top - imgLen) / 256) * 256;
    const S = imgStart - slen;               // stream start in high RAM
    const L = slen + imgLen;
    if (!d.tail && !inRam37(imgStart, imgStart + imgLen)) return { why: 'depacker under ROM' };
    const S1LEN = noio ? 25 : 30;
    const CS = 0x0801 + 10 + S1LEN;          // chunk position in the loaded file
    if (noio) {
        // no sei / $01 change: everything must stay in RAM that is plain RAM with
        // $01 = $37, away from the KERNAL's own pages, with the IRQ still running
        if (imgStart + imgLen > 0xa000) return { why: 'no-I/O mode needs the data below $a000' };
        for (const b of blocks) if (b.addr < 0x0800 || b.addr + b.data.length > 0xa000) return { why: 'no-I/O mode needs every block in $0800-$9fff' };
    }
    if (S - CS < 512) return { why: 'too close to the loaded file' };
    if (S < 0x0200) return { why: 'does not fit' };
    // replay: global read position must stay ahead of every output byte
    for (const p of parts) {
        const b = blocks[p.i], tr = enc.traces[p.i];
        for (let k = 0; k < b.data.length; k++) {
            const a = b.addr + k, rd = S + p.data + tr.used[k];
            if ((a >= rd && a < S + slen) || (a >= imgStart && a < imgStart + imgLen))
                return { why: `${b.name}: output ${hex4(a)} would hit ${a >= imgStart ? 'the depacker' : 'unread data'}` };
        }
    }
    // image: relocate, table, operands, final bytes
    const img = new Uint8Array(imgLen);
    img.set(img0, 0);
    for (const o of d.reloc_hi) img[o] = (img[o] - (d.origin >> 8) + (imgStart >> 8)) & 0xff;
    if (d.table) img.set(enc.table45, d.sym.tabspace);
    img[d.sym.srcop] = (S - 1) & 0xff; img[d.sym.srcop + 1] = (S - 1) >> 8;   // pre-increment
    if (d.tail) img[d.sym.tlen] = fin.length - 1;
    if (single) {                            // end of the only block: go to fin directly
        const disp = d.sym.fin - (d.sym.endbr + 1);
        if (disp > 127) throw new Error('internal: end branch out of range');
        img[d.sym.endbr] = disp;
    }
    img.set(fin, img0.length);                        // after the code (TAIL: after the copy loop)
    const chunk = new Uint8Array(L);
    for (const p of parts) {
        const b = blocks[p.i];
        chunk[p.hdr] = b.addr >> 8; chunk[p.hdr + 1] = b.addr & 0xff;
        chunk.set(enc.streams[p.i], p.data);
    }
    if (!single) chunk[slen - 1] = 0;
    chunk.set(img, slen);
    // stage 1: copy pages [CS + L - 256P, CS + L) -> [S + L - 256P, S + L)
    const P = Math.ceil(L / 256), srcPg = CS + L - 256, dstPg = S + L - 256, entry = imgStart + d.sym.blk;
    if (P > 255) return { why: 'too much data' };
    // stage 1 at $080b; its first two bytes "a0 00" double as BASIC's end-of-program link
    const lp = 0x080b + (noio ? 4 : 9);
    const s1 = [0xa0, 0x00];                                  // ldy #0
    if (!noio) s1.push(0x78, 0xa9, 0x34, 0x85, 0x01);          // sei, lda #$34, sta $01
    s1.push(0xa2, P,                                          // ldx #pages
        0xb9, srcPg & 0xff, srcPg >> 8,                       // lp: lda src,y
        0x99, dstPg & 0xff, dstPg >> 8,                       //     sta dst,y
        0xc8, 0xd0, 0xf7,                                     //     iny, bne lp
        0xce, (lp + 2) & 0xff, (lp + 2) >> 8,                 //     dec lp+2
        0xce, (lp + 5) & 0xff, (lp + 5) >> 8,                 //     dec lp+5
        0xca, 0xd0, 0xee,                                     //     dex, bne lp
        0x4c, entry & 0xff, entry >> 8);                      //     jmp blk
    if (s1.length !== S1LEN) throw new Error('internal: stage 1');
    // 0 SYS2059: link to $080b, whose high byte ($080c) is the 00 of "ldy #0"
    const stub = [0x0b, 0x08, 0x00, 0x00, 0x9e, 0x32, 0x30, 0x35, 0x39, 0x00];
    const prg = new Uint8Array(2 + 10 + S1LEN + L);
    prg.set([0x01, 0x08]); prg.set(stub, 2); prg.set(s1, 12); prg.set(chunk, 12 + S1LEN);
    if (verbose) console.log(`  ${variant} top ${hex4(top)}: ${prg.length} B`);
    return { prg, S, imgStart, imgLen, L, variant, top, depackBytes: 10 + S1LEN + imgLen - (d.table ? 45 : 0), tableBytes: d.table, headerBytes: 2 * ord.length + (ord.length > 1 ? 1 : 0),
        streamBytes: ord.reduce((t, i) => t + enc.streams[i].length, 0), info: `data moved to ${hex4(S)}, depacker ${hex4(imgStart)}-${hex4(imgStart + imgLen - 1)}` };
}


// ---- backward, in place --------------------------------------------------------
// PRG: [BASIC line][terminator if several blocks][blocks, ascending address order:
// reverse(stream) then dst-end lo, hi][stage][depacker image][tape payload].
// Each stream is AMYZ(reverse(data)); stored reversed, it is read from its end.
// The stage copies the depacker image to $0100 and the tape payload (table, final
// code, raw prefix) to $0334, then decrunching runs from the top of the blocks
// downward: the output of a block is written from its end, so data still to be read
// (below) is overwritten only if the writer catches up with the reader. When it
// would, the first m bytes of the lowest block are not compressed: they travel in
// the tape payload and are copied to their place by the final code.
const IMG_MAX = 0xe0, TAPE = 0x0334, TAPE_MAX = 0x0400 - 0x0334;
function layoutBackward(blocks, kind, t15, widths, t45, encode, fmode, execAddr, basicEnd, noio) {
    const d = DEPACK['b' + kind];
    const ord = blocks.map((_, i) => i).sort((x, y) => blocks[y].addr - blocks[x].addr);   // decode order: highest first
    const low = ord[ord.length - 1];
    const single = ord.length === 1;
    for (const b of blocks) {
        if (b.addr < 0x0400 && b.addr + b.data.length > TAPE) return { why: 'a block covers $0334-$03ff' };
        if (noio && (b.addr < 0x0800 || b.addr + b.data.length > 0xa000)) return { why: 'no-I/O mode needs every block in $0800-$9fff' };
    }
    let m = 0;
    for (let iter = 0; iter < 12; iter++) {
        // encode
        const parts = ord.map((i) => {
            const b = blocks[i], skip = i === low ? m : 0;
            const data = b.data.subarray(skip), rev = data.slice().reverse();
            const z = encode(rev);
            const tr = decodeTrace(z, widths);
            if (tr.out.length !== rev.length || tr.out.some((v, k) => v !== rev[k])) throw new Error('round trip failed (backward): ' + b.name);
            return { i, b, skip, n: data.length, z, used: tr.used };
        });
        // tape payload: [table][raw prefix copy + final code][raw prefix bytes]
        const tabLen = d.table ? 45 : 0;
        const finAddr = TAPE + tabLen;
        const lowB = blocks[low];
        const code = [];
        if (m) code.push(0xa2, m, 0xbd, 0, 0, 0x9d, (lowB.addr - 1) & 0xff, (lowB.addr - 1) >> 8, 0xca, 0xd0, 0xf7); // ldx #m, lda raw-1,x, sta dst-1,x, dex, bne
        code.push(...finalBytes(fmode, execAddr, basicEnd, noio));
        const rawAt = finAddr + code.length;
        if (m) { code[3] = (rawAt - 1) & 0xff; code[4] = (rawAt - 1) >> 8; }
        const tape = new Uint8Array(tabLen + code.length + m);
        if (tabLen) tape.set(t45, 0);
        tape.set(code, tabLen); tape.set(lowB.data.subarray(0, m), tabLen + code.length);
        if (tape.length > TAPE_MAX) return { why: `tape payload ${tape.length} B > ${TAPE_MAX} (raw prefix ${m})` };
        if (m > 255) return { why: 'raw prefix too long' };
        // depacker image at $0100
        const img0 = Uint8Array.from(Buffer.from(d.image, 'hex'));
        const img = new Uint8Array(img0.length + 3);
        img.set(img0);
        for (const o of d.reloc_hi) img[o] = (img[o] - (d.origin >> 8) + 0x01) & 0xff;
        img.set([0x4c, finAddr & 0xff, finAddr >> 8], img0.length);
        if (single) img[d.sym.endbr] = d.sym.fin - (d.sym.endbr + 1);
        if (img.length > IMG_MAX) return { why: 'depacker image too big for $0100' };
        // blocks region
        const blob = [];
        if (!single) blob.push(0);
        const pos = new Map();
        for (const pt of parts.slice().reverse()) {                // ascending address order
            const st = blob.length; for (let k = pt.z.length - 1; k >= 0; k--) blob.push(pt.z[k]);
            const end = pt.b.addr + pt.skip + pt.n;
            blob.push(end & 0xff, end >> 8);
            pos.set(pt.i, { start: st, end: blob.length });     // [start, end) incl. header
        }
        // layout with the BASIC line (its length depends on the SYS digits)
        const stageLen = (noio ? 0 : 5) + 22 + 2 + 3;
        let stubLen = 12, sys = 0;
        for (let k = 0; k < 2; k++) { sys = 0x0801 + stubLen + blob.length; stubLen = 8 + String(sys).length; }
        const blobBase = 0x0801 + stubLen, blobEnd = blobBase + blob.length;
        const imgAt = blobEnd + stageLen, tapeAt = imgAt + img.length, prgEnd = tapeAt + tape.length;
        if (noio && prgEnd > 0xa000) return { why: 'no-I/O mode needs the file below $a000' };
        if (prgEnd > 0x10000) return { why: 'does not fit' };
        // replay: a write may not land on stream bytes not read yet (below the reader)
        let R = blobEnd, viol = null;
        for (const pt of parts) {
            R -= 2;                                                   // header
            const Rb = R, A = pt.b.addr + pt.skip;
            for (let k = 0; k < pt.n && !viol; k++) {
                const W = A + pt.n - 1 - k;
                if (W >= blobBase && W < Rb - pt.used[k]) viol = { W, pt };
                if ((W >= 0x0100 && W < 0x0200) || (W >= TAPE && W < 0x0400)) return { why: 'output over the relocated depacker' };
            }
            if (viol) break;
            R = Rb - pt.z.length;
        }
        if (viol) {
            if (viol.pt.i !== low) return { why: `block ${viol.pt.b.name} would overwrite unread data at ${hex4(viol.W)}` };
            const need = viol.W - blocks[low].addr + 1;
            m = Math.max(m + 1, need);
            continue;
        }
        // stage: [sei, $01 = $34] copy image to $0100, tape to $0334, ldy #0, jmp blk
        const st = [];
        if (!noio) st.push(0x78, 0xa9, 0x34, 0x85, 0x01);
        st.push(0xa2, img.length, 0xbd, (imgAt - 1) & 0xff, (imgAt - 1) >> 8, 0x9d, 0xff, 0x00, 0xca, 0xd0, 0xf7);
        st.push(0xa2, tape.length, 0xbd, (tapeAt - 1) & 0xff, (tapeAt - 1) >> 8, 0x9d, 0x33, 0x03, 0xca, 0xd0, 0xf7);
        st.push(0xa0, 0x00, 0x4c, (0x0100 + d.sym.blk) & 0xff, (0x0100 + d.sym.blk) >> 8);
        if (st.length !== stageLen) throw new Error('internal: stage');
        // stream start operand: one past the top block's header
        img[d.sym.srcop] = blobEnd & 0xff; img[d.sym.srcop + 1] = blobEnd >> 8;
        const digits = String(sys);
        const stub = [(0x0801 + stubLen - 2) & 0xff, (0x0801 + stubLen - 2) >> 8, 0x00, 0x00, 0x9e, ...Buffer.from(digits, 'latin1'), 0x00, 0x00, 0x00];
        if (stub.length !== stubLen) throw new Error('internal: stub');
        const prg = new Uint8Array(2 + prgEnd - 0x0801);
        prg.set([0x01, 0x08]); prg.set(stub, 2);
        prg.set(blob, 2 + stubLen); prg.set(st, 2 + stubLen + blob.length);
        prg.set(img, 2 + imgAt - 0x0801); prg.set(tape, 2 + tapeAt - 0x0801);
        const streamBytes = parts.reduce((t, p) => t + p.z.length, 0);
        return { prg, variant: 'b' + kind, depackBytes: stubLen + stageLen + img.length + code.length, tableBytes: tabLen,
            headerBytes: 2 * parts.length + (single ? 0 : 1), streamBytes, rawPrefix: m,
            info: `in place, blocks ${hex4(blobBase)}-${hex4(blobEnd - 1)}, depacker $0100-${hex4(0x0100 + img.length - 1)}, tape payload $0334-${hex4(0x0334 + tape.length - 1)}`,
            order: ord, top: 0 };
    }
    return { why: 'no margin found' };
}

async function main(argv) {
    const files = []; let outName = 'packed.prg', speed = 0, dir = 'best', tabz = null, force = false, exec = null, top = null, mode = 'best', search = false, verbose = false;
    for (let a = 0; a < argv.length; a++) {
        const s = argv[a];
        if (s === '-o') outName = argv[++a];
        else if (s === '-x') exec = argv[++a];
        else if (s === '-top') top = parseAddr(argv[++a]);
        else if (s === '-mode') mode = argv[++a];
        else if (s === '-search') search = true;
        else if (s === '-v') verbose = true;
        else if (s === '-tabz') tabz = argv[++a];
        else if (s === '-force') force = true;
        else if (s === '-speed') speed = +argv[++a];
        else if (s === '-dir') dir = argv[++a];
        else if (s.startsWith('-')) throw new Error('unknown option ' + s);
        else files.push(s);
    }
    if (!files.length) throw new Error('usage: amyzpack4 [-o out.prg] [-x addr|basic] [-mode tiny|table|best] [-top addr] file.prg [raw.bin@addr ...]');
    const blocks = files.map((f) => {
        const m = f.match(/^(.*)@(.+)$/);
        if (m) return { name: m[1], addr: parseAddr(m[2]), data: new Uint8Array(readFileSync(m[1])) };
        const d = new Uint8Array(readFileSync(f));
        return { name: f, addr: d[0] | d[1] << 8, data: d.subarray(2) };
    });
    for (const b of blocks) {
        if (!b.data.length) throw new Error(b.name + ': empty');
        if (b.addr < 0x200 || b.addr + b.data.length > 0x10000) throw new Error(b.name + ': must lie in $0200-$ffff');
    }
    for (let i = 0; i < blocks.length; i++) for (let j = i + 1; j < blocks.length; j++) {
        const a = blocks[i], b = blocks[j];
        if (a.addr < b.addr + b.data.length && b.addr < a.addr + a.data.length) throw new Error(a.name + ' overlaps ' + b.name);
    }
    let fmode = 0, execAddr = 0, basicEnd = 0;
    // a program at $0801 whose BASIC part is a single "SYS nnnn" line is started
    // with jmp nnnn (like Exomizer's "sfx sys"); -x basic forces BASIC RUN
    if (exec === null && blocks[0].addr === 0x0801) {
        const d = blocks[0].data, m = Buffer.from(d.subarray(4, 24)).toString('latin1').match(/^\x9e ?\(?(\d+)/);
        const next = d[0] | d[1] << 8;
        if (m && next > 0x0801 && next - 0x0801 + 1 < d.length && d[next - 0x0801 + 1] === 0) exec = m[1];
    }
    if (exec === 'basic' || (exec === null && blocks[0].addr === 0x0801)) { fmode = 1; basicEnd = blocks[0].addr + blocks[0].data.length; }
    else execAddr = exec === null ? blocks[0].addr : parseAddr(exec);

    const lib = await import('./lib/amyz.js');
    if (speed) { const { speedModel } = await import('./speedmodel.mjs'); lib.setCostModel(speedModel('6502', speed)); }
    const table45 = (w) => {
        const t45 = new Uint8Array(45); let base = 1;
        for (let k = 0; k < 15; k++) {
            const adj = (base - 2 ** w[k]) & 0xffff;
            t45[k] = adj & 0xff; t45[15 + k] = adj >> 8; t45[30 + k] = w[k];
            base += 2 ** w[k];
        }
        return t45;
    };
    // a table for a set of datas: given (-tabz) or fitted (fit always with the plain size model)
    const fitTable = (datas) => {
        if (tabz) return new Uint8Array(readFileSync(tabz)).subarray(0, 15);
        lib.setMaxOffset(65535);
        const save = speed; if (save) lib.setCostModel(null);
        const fit = lib.fitGroupAMYZ(datas, search ? {} : { search: false });
        if (save) lib.setCostModel(speedModelSync('6502', speed));
        return fit.table;
    };
    const kinds = [];
    if (mode === 'tiny' || mode === 'best') kinds.push('tiny');
    if (mode === 'table' || mode === 'best') kinds.push('table');
    const enc1 = (kind, t15, data) => { lib.setMaxOffset(kind === 'tiny' ? 32767 : 65535); return lib.compressTable(data, t15); };
    let best = null; const tried = [];
    const consider = (r, label) => {
        tried.push(`${label}: ${r.why || r.prg.length + ' B'}`);
        if (r.prg && (!best || r.prg.length < best.prg.length)) best = r;
    };

    // forward (amyzpack3 layout: data moved to high RAM, decrunched forward)
    if (dir !== 'bwd') for (const kind of kinds) {
        const t15 = kind === 'tiny' ? Uint8Array.from(LOG2, (w) => w + 1) : fitTable(blocks.map((b) => b.data));
        const widths = Array.from(t15, (x) => x - 1);
        const enc = { kind, widths, streams: blocks.map((b) => enc1(kind, t15, b.data)), table45: table45(widths) };
        enc.traces = enc.streams.map((z, i) => {
            const tr = decodeTrace(z, enc.widths);
            if (tr.out.length !== blocks[i].data.length || tr.out.some((v, k) => v !== blocks[i].data[k])) throw new Error('round trip failed: ' + blocks[i].name);
            return tr;
        });
        const orders = [blocks.map((_, i) => i)];
        orders.push(orders[0].slice().sort((x, y) => blocks[y].addr - blocks[x].addr));
        orders.push(orders[0].slice().sort((x, y) => blocks[x].addr - blocks[y].addr));
        const tops = top !== null ? [top] : [0xd000, 0xa000, 0xfff0];
        const cands = [];
        for (const t of tops) for (const tail of [false, true]) cands.push({ t, tail, noio: false });
        if (top === null || top <= 0xa000) cands.push({ t: top ?? 0xa000, tail: false, noio: true });
        for (const order of orders) {
            enc.order = order;
            for (const { t, tail, noio } of cands) {
                const fin = finalBytes(fmode, execAddr, basicEnd, noio);
                const r = layout(blocks, enc, enc.kind + (tail ? '_tail' : ''), t, fin, verbose, noio);
                if (r.prg) Object.assign(r, { noio, enc, order: order.slice(), dir: 'fwd' });
                consider(r, `fwd ${enc.kind}${tail ? '_tail' : ''}${noio ? ' no-I/O' : ''} top ${hex4(t)}`);
            }
        }
    }
    // backward (in place: no data copy; depacker to $0100, table/final/raw prefix to $0334)
    if (dir !== 'fwd') for (const kind of kinds) {
        const rev = (d) => d.slice().reverse();
        const t15 = kind === 'tiny' ? Uint8Array.from(LOG2, (w) => w + 1) : fitTable(blocks.map((b) => rev(b.data)));
        const widths = Array.from(t15, (x) => x - 1);
        for (const noio of [false, true]) {
            const r = layoutBackward(blocks, kind, t15, widths, table45(widths), (d) => enc1(kind, t15, d), fmode, execAddr, basicEnd, noio);
            if (r.prg) Object.assign(r, { noio, dir: 'bwd' });
            consider(r, `bwd ${kind}${noio ? ' no-I/O' : ''}`);
        }
    }
    if (!best) throw new Error('no safe layout:\n  ' + tried.join('\n  '));
    // never grow a single .prg: keep the original when packing does not help
    if (!force && files.length === 1 && !/@/.test(files[0])) {
        const orig = readFileSync(files[0]);
        if (best.prg.length >= orig.length) {
            writeFileSync(outName, orig);
            console.log(`${outName}: packing gives ${best.prg.length} B, not smaller than the original ${orig.length} B: original kept (use -force to pack anyway)`);
            return;
        }
    }
    writeFileSync(outName, best.prg);
    if (verbose) console.log(tried.join('\n'));
    const raw = blocks.reduce((s, b) => s + b.data.length, 0);
    console.log(`${outName}: ${best.prg.length} B from ${raw} B. ${best.dir === 'bwd' ? 'backward in place' : 'forward'} ${best.variant}${best.noio ? ' no-I/O' : ''}: depacker ${best.depackBytes} B, table ${best.tableBytes} B, block headers ${best.headerBytes} B, streams ${best.streamBytes} B${best.rawPrefix ? `, raw prefix ${best.rawPrefix} B` : ''}${speed ? `, speed ${speed}` : ''}`);
    console.log(`  ${best.info}, start: ${fmode ? 'BASIC RUN' : hex4(execAddr)}`);
}

main(process.argv.slice(2)).catch((e) => { console.error('amyzpack4: ' + e.message); process.exit(1); });
