import assert from "node:assert/strict";
import {
  compressTable,
  compressTiny,
  decompressTable,
  decompressTiny,
  expandTable,
  fitGroupAMYZ,
} from "../encoders/js/amyz.js";

const vectors = [
  Uint8Array.from(Buffer.from("ABC-ABC+ABC", "ascii")),
  Uint8Array.from({ length: 1024 }, (_, i) => (i * 37 + (i >> 3)) & 255),
  Uint8Array.from({ length: 2048 }, (_, i) => (i & 63) < 48 ? 0x55 : i & 255),
];

for (const input of vectors) {
  const packed = compressTiny(input);
  assert.deepEqual(decompressTiny(packed), input);
}

const fitted = fitGroupAMYZ(vectors);
assert.equal(fitted.table.length, 15);
assert.equal(expandTable(fitted.table).length, 45);
for (let i = 0; i < vectors.length; i++) {
  assert.deepEqual(decompressTable(fitted.streams[i], fitted.table), vectors[i]);
  assert.deepEqual(decompressTable(compressTable(vectors[i], fitted.table), fitted.table), vectors[i]);
}

console.log("AMYZ smoke test: PASS");

