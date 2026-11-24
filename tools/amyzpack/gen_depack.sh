#!/bin/sh
# Assemble the depacker variants at $1000 and $2000 and write depack.json:
# image (origin $1000), relocated high-byte offsets, symbol offsets.
#   forward (depack_fwd.s): table, tiny, table_tail, tiny_tail
#   backward (depackb.s):   btable, btiny
set -e
CA65=${CA65:-ca65}; LD65=${LD65:-ld65}
T=$(mktemp -d)
for v in table tiny table_tail tiny_tail btable btiny; do
  D=""; SRC=depack_fwd.s
  case $v in b*) SRC=depackb.s;; esac
  case $v in tiny*|btiny) D="$D -D TINY";; esac
  case $v in *tail) D="$D -D TAIL";; esac
  $CA65 $D $SRC -o $T/d.o
  for o in 1000 2000; do
    printf 'MEMORY { M: start=$%s, size=$1000, file=%%O; }\nSEGMENTS { CODE: load=M, type=ro; }\n' $o > $T/c.cfg
    $LD65 -C $T/c.cfg -o $T/$v.$o.bin -Ln $T/$v.$o.lbl $T/d.o
  done
done
python3 - "$T" <<'P'
import sys, json
t = sys.argv[1]; out = {}
for v in ('table', 'tiny', 'table_tail', 'tiny_tail', 'btable', 'btiny'):
    a = open(f'{t}/{v}.1000.bin', 'rb').read(); b = open(f'{t}/{v}.2000.bin', 'rb').read()
    rel = [i for i, (x, y) in enumerate(zip(a, b)) if x != y]
    assert all((b[i] - a[i]) & 0xff == 0x10 for i in rel)
    sym = {}
    for line in open(f'{t}/{v}.1000.lbl'):
        _, addr, name = line.split()
        sym[name.lstrip('.')] = int(addr, 16) - 0x1000
    out[v] = {'image': a.hex(), 'reloc_hi': rel, 'origin': 0x1000, 'sym': sym,
              'table': 0 if 'tiny' in v else 45, 'tail': v.endswith('tail'), 'backward': v.startswith('b')}
json.dump(out, open('depack.json', 'w'), indent=1)
print({v: len(out[v]['image']) // 2 for v in out})
P
rm -rf $T
