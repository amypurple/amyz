#!/bin/sh
# run a port-parameterised Z80 VRAM decoder on all corpus streams
# usage: VDPDATA=98 run_port.sh code.bin tabext(tabz|tabzx) STREAMS TABLES CORPUS WGAME
V=$(dirname "$0")/vtest_port
ok=0; fail=0; T=0; N=0; gap=999; v29=0
while IFS="$(printf '\t')" read cat name sz game; do
  o=$5/$cat/$name; [ $cat = warrior ] && o=$6/warrior/$name
  r=$($V $1 $3/$cat/$name $o 0 $4/$game.$2)
  case "$r" in OK*) ok=$((ok+1));; *) fail=$((fail+1)); echo "FAIL $name $r";; esac
  t=$(echo "$r" | sed 's/.* T=\([0-9]*\).*/\1/'); n=$(echo "$r" | sed 's/.* out=\([0-9]*\).*/\1/')
  g=$(echo "$r" | sed 's/.*min_data_gap=\([0-9]*\).*/\1/'); u=$(echo "$r" | sed 's/.*gaps_under_29=\([0-9]*\).*/\1/')
  T=$((T+t)); N=$((N+n)); [ $g -gt 0 ] && [ $g -lt $gap ] && gap=$g; v29=$((v29+u))
done < $3/files.tsv
echo "ok $ok fail $fail  T/byte $(echo "scale=1; $T/$N" | bc)  smallest data gap $gap T  gaps under 29: $v29"
