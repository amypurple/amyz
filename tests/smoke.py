#!/usr/bin/env python3
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "encoders" / "python"))

from amyz import (  # noqa: E402
    compress_table,
    compress_tiny,
    decompress_table,
    decompress_tiny,
    expand_table,
    fit_group,
)

vectors = [
    b"ABC-ABC+ABC",
    bytes((i * 37 + (i >> 3)) & 255 for i in range(1024)),
    bytes(0x55 if (i & 63) < 48 else i & 255 for i in range(2048)),
]

for data in vectors:
    assert decompress_tiny(compress_tiny(data)) == data

table, streams = fit_group(vectors)
assert len(table) == 15
assert len(expand_table(table)) == 45
for data, stream in zip(vectors, streams):
    assert decompress_table(stream, table) == data
    assert decompress_table(compress_table(data, table), table) == data

print("AMYZ Python smoke test: PASS")

