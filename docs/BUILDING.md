# Building the encoders

## JavaScript

Node.js 18 or later is sufficient. There are no package dependencies.

```text
npm test
```

## Python

The readable reference implementation requires Python 3 and no external
packages.

```text
python tests/smoke.py
python encoders/python/amyz.py
```

## C

Build the optimized encoder with a C99 compiler:

```text
gcc -O3 -o amyzenc encoders/c/amyzenc.c encoders/c/amyzc_core.c -lm
```

Run `amyzenc` without arguments for its usage summary. The C encoder is the
preferred final-release tool when its broader table search finds a smaller
group result; JavaScript remains useful for browser integration and rapid
iteration.

## Assembly decoders

The Z80 release measurements used sjasmplus. The 6502 ports use ca65, and the
TMS9900 ports use xas99. Decoder source has no dependency on the host encoders;
the only shared contract is the AMYZ stream and table described in `SPEC.md`.

