import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";

const html = readFileSync(new URL("../tools/amyz-web-packer.html", import.meta.url), "utf8");
assert.match(html, /^<!doctype html>/i, "Web packer must be a complete HTML document");
assert.doesNotMatch(html, /AMYZ2/i, "Rejected AMYZ2 format must not appear in the public packer");

const match = html.match(/const WORKER_SRC = ("(?:[^"\\]|\\.)*");/s);
assert.ok(match, "Cannot locate the embedded AMYZ worker");
const workerSource = JSON.parse(match[1]);
const messages = [];
const self = { postMessage(message) { messages.push(message); } };
const context = vm.createContext({
  self,
  console,
  Uint8Array,
  Uint16Array,
  Uint32Array,
  Int32Array,
  ArrayBuffer,
  DataView,
  Map,
  Set,
  Math,
  Promise,
  TextEncoder,
  TextDecoder,
});

vm.runInContext(workerSource, context, { filename: "amyz-web-worker.js", timeout: 10_000 });
assert.equal(typeof self.onmessage, "function", "Embedded worker did not register its handler");

const input = Uint8Array.from({ length: 768 }, (_, index) => ((index >> 3) ^ (index * 7)) & 0x3f);
await self.onmessage({
  data: {
    job: "raw",
    files: [{ name: "smoke.bin", data: input }],
    fast: false,
    search: false,
    maxOffset: 16383,
    normalize: null,
    compare: [],
  },
});

const failure = messages.find((message) => message.type === "error");
assert.equal(failure, undefined, failure?.message || "Web worker failed");
const result = messages.find((message) => message.type === "done");
assert.ok(result, "Web worker did not return a packed result");
assert.equal(result.streams.length, 1);
assert.equal(result.stats[0].out, input.length);
assert.equal(result.table.length, 15);
assert.equal(result.tabzx.length, 45);

console.log("AMYZ web packer smoke test: PASS");
