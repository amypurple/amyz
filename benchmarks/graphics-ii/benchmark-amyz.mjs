#!/usr/bin/env node
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import {
  compressTiny,
  decompressTable,
  decompressTiny,
  fitGroupAMYZ,
} from "../../encoders/js/amyz.js";

const here = path.dirname(fileURLToPath(import.meta.url));
const corpus = path.resolve(process.argv[2] || "");
assert.ok(corpus && fs.existsSync(corpus), "usage: node benchmark-amyz.mjs CORPUS_DIR [OUTPUT.json]");
const output = path.resolve(process.argv[3] || path.join(here, "amyz-results.json"));
const manifest = JSON.parse(fs.readFileSync(path.join(corpus, "manifest.json"), "utf8"));
const hash = (data) => createHash("sha256").update(data).digest("hex");
const equal = (actual, expected, label) => {
  assert.deepEqual(Buffer.from(actual), expected, `${label} did not round-trip`);
};

const rows = [];
for (const entry of manifest) {
  const directory = path.join(corpus, entry.id);
  const pattern = fs.readFileSync(path.join(directory, "pattern.bin"));
  const color = fs.readFileSync(path.join(directory, "color.bin"));
  assert.equal(pattern.length, 6144);
  assert.equal(color.length, 6144);

  const fitted = fitGroupAMYZ([pattern, color]);
  equal(decompressTable(fitted.streams[0], fitted.table), pattern, `${entry.id}/pattern shared`);
  equal(decompressTable(fitted.streams[1], fitted.table), color, `${entry.id}/color shared`);

  const tinyPattern = compressTiny(pattern);
  const tinyColor = compressTiny(color);
  equal(decompressTiny(tinyPattern), pattern, `${entry.id}/pattern tiny`);
  equal(decompressTiny(tinyColor), color, `${entry.id}/color tiny`);

  const sharedPayload = fitted.streams[0].length + fitted.streams[1].length;
  const tinyPayload = tinyPattern.length + tinyColor.length;
  rows.push({
    ...entry,
    rawBytes: pattern.length + color.length,
    sourceSha256: hash(Buffer.concat([pattern, color])),
    sharedPayload,
    sharedTable: 15,
    sharedFirstUse: sharedPayload + 15 + 182,
    fastFirstUse: sharedPayload + 45 + 203,
    tinyPayload,
    tinyFirstUse: tinyPayload + 152,
  });
  console.log(`${entry.label}: shared ${sharedPayload}, tiny ${tinyPayload}`);
}

const sum = (field) => rows.reduce((total, row) => total + row[field], 0);
const result = {
  format: "AMYZ 1.0",
  method: "One fitted 15-byte table shared by each picture's pattern and color tables",
  decoderBytes: { shared: 182, fast: 203, tiny: 152 },
  tableBytes: { shared: 15, fast: 45, tiny: 0 },
  pictures: rows.length,
  totals: {
    rawBytes: sum("rawBytes"),
    sharedPayload: sum("sharedPayload"),
    sharedTables: rows.length * 15,
    tinyPayload: sum("tinyPayload"),
  },
  rows,
};
fs.writeFileSync(output, `${JSON.stringify(result, null, 2)}\n`);
console.log(`Wrote ${output}`);

