# Notices and credits

AMYZ was designed by Amy Bienvenu in 2026, continuing her DAN1-DAN3 compression
work, with research, implementation and evaluation assistance from Claude and
Codex.

The design acknowledges these prior works:

- **ZX0**, by Einar Saukas: optimal parsing, implicit-state LZ grammar,
  repeat-last-offset tokens and interlaced Elias gamma coding. ZX0's compressor
  is BSD-3-Clause; its repository describes separate conditions for using its
  decompressors. <https://github.com/einar-saukas/ZX0>
- **Exomizer**, by Magnus Lind: inspiration for fitted offset buckets and for
  evaluating complete self-extracting packers rather than payload alone.
  <https://bitbucket.org/magli143/exomizer/wiki/Home>

AMYZ does not include source files from ZX0 or Exomizer. Names and measurements
are included for attribution and technical comparison.

Some benchmark inputs depict third-party artwork. The public AMYZ repository
contains measurements and manifests, not those raw images, unless their
redistribution rights are known. Each external tool used for comparison remains
under its own license.

