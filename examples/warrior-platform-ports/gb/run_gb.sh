#!/bin/sh
# run an SM83 decoder image on every corpus stream
# usage: run_gb.sh code.bin tabext(tabz|tabzx) STREAMS TABLES CORPUS WGAME
V=$(dirname "$0")/gbtest
ok=0; fail=0; T=0; N=0
while IFS="$(printf '\t')" read cat name sz game; do
  o=$5/$cat/$name; [ $cat = warrior ] && o=$6/warrior/$name
  r=$($V $1 $3/$cat/$name $o $4/$game.$2)
  case "$r" in OK*) ok=$((ok+1));; *) fail=$((fail+1)); echo "FAIL $name $r";; esac
  t=$(echo "$r" | sed 's/.* T=\([0-9]*\).*/\1/'); n=$(echo "$r" | sed 's/.* out=\([0-9]*\).*/\1/')
  T=$((T+t)); N=$((N+n))
done < $3/files.tsv
echo "ok $ok fail $fail  total out $N bytes  T $T  T/byte $(echo "scale=1; $T/$N" | bc)"
