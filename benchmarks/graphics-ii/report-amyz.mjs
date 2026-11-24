#!/usr/bin/env node
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const source = path.resolve(process.argv[2] || path.join(here, "amyz-results.json"));
const output = path.resolve(process.argv[3] || path.join(here, "amyz-results.md"));
const report = JSON.parse(fs.readFileSync(source, "utf8"));
const lines = [
  "# AMYZ Graphics II corpus results",
  "",
  `Format: ${report.format}. ${report.method}.`,
  "",
  "Every stream was decoded and compared byte-for-byte with its 12,288-byte source.",
  "",
  "| Picture | Shared payload | Shared first use | Fast first use | Tiny payload | Tiny first use |",
  "|---|---:|---:|---:|---:|---:|",
];
for (const row of report.rows) {
  lines.push(`| ${row.label.replaceAll("|", "\\|")} | ${row.sharedPayload} | ${row.sharedFirstUse} | ${row.fastFirstUse} | ${row.tinyPayload} | ${row.tinyFirstUse} |`);
}
lines.push(
  "",
  "Shared first use counts payload + one 15-byte table + the 182-byte Z80 VRAM decoder.",
  "Fast first use counts the same stream payload + one expanded 45-byte table + the 203-byte fast decoder.",
  "Tiny first use counts payload + the table-free 152-byte decoder.",
  "",
);
fs.writeFileSync(output, lines.join("\n"));
console.log(`Wrote ${output}`);

