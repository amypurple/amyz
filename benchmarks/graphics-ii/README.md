# Graphics II compression corpus

The Amy Studio Graphics II corpus contains 42 unique 256x192 pictures. Each
sample is represented by a 6,144-byte TMS9918A pattern table and a 6,144-byte
color table, for 12,288 raw bytes per picture.

The files in this directory preserve the cross-codec baseline measured before
AMYZ was added. They cover payload ratios, decoder sizes and representative
first-use costs. The corpus includes both highly compressible game screens and
difficult converted artwork.

AMYZ was evaluated in two explicit ways:

- **Standalone picture:** pattern and color share one 15-byte table.
- **Game group:** several pictures and other assets share one table, counted
  once for the complete group.

The second case is AMYZ's intended strength but must not be substituted for the
first in a per-picture ranking.

## AMYZ 1.0 result

| Measurement | Bytes | Ratio |
|---|---:|---:|
| Raw corpus | 516,096 | 100% |
| AMYZ payload | **161,148** | **31.22%** |
| AMYZ payload + 42 private 15-byte tables | 161,778 | 31.35% |
| AMYZ Tiny payload | 165,625 | 32.09% |
| Exomizer 2 payload baseline | 161,374 | 31.27% |
| ZX0 payload baseline | 166,556 | 32.27% |

AMYZ's payload is 226 bytes smaller than Exomizer 2 and 5,408 bytes smaller
than ZX0. Once 42 private tables are charged, Exomizer's payload is 404 bytes
smaller. This artificial all-unrelated-pictures aggregate therefore illustrates
why both payload and table ownership must be reported.

On individual pictures, where Pattern and Color share one table, AMYZ beats:

- ZX0 on 42/42 payloads and 36/42 first-use totals;
- Exomizer 2 on 31/42 payloads and 32/42 first-use totals.

`amyz-results.json` records every source SHA-256, payload and first-use total.
`amyz-results.md` is its readable report. Regenerate both from an exported
corpus with `node benchmark-amyz.mjs CORPUS_DIR`, followed by
`node report-amyz.mjs`.

Raw pictures are not included here when their artwork redistribution rights are
unknown. The authoritative corpus builder remains in Amy Studio; this repository
retains the measurement contract and published results.


