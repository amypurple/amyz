# Rejected experiments

AMYZ keeps one public stream format. This file records substantial alternatives
that were measured and rejected, so their lessons are preserved without making
the user choose between near-duplicate formats.

## AMYZ2 token experiment

AMYZ2 changed the token representation. It was tested with Z80 and 6502
decoders and with both compact and fast 6502 depackers.

It was rejected because:

- measured decode speed improved by only about 5% on Z80 and 8% on 6502;
- its net ColecoVision size exceeded the project's 1% acceptance threshold;
- its Z80 decoder occupied IX;
- AMYZ's existing `-speed` search objective produced comparable or better
  size/speed choices without changing the stream format;
- a numbered AMYZ2 would imply that AMYZ had been superseded and would make
  compatibility and decoder selection less clear.

One representative fast-depacker corpus measured:

| Configuration | Total size | Decode cycles |
|---|---:|---:|
| AMYZ2 fast, speed 0 | 68,257 bytes | 7.71 million |
| AMYZ fast, speed 8 | 68,847 bytes | 7.27 million |

The public design therefore remains:

- **AMYZ** identifies the stream format;
- **`-speed`** changes the encoder's size/speed objective;
- **`-fast`** selects a larger, faster compatible depacker;
- table representation and decoder destination remain implementation profiles,
  not new format versions.

The original experiment, source, harnesses and measurements are retained in the
project archive as `AMYZ2-EXPERIMENT.zip`. They are historical evidence, not a
supported release or compatibility target.
