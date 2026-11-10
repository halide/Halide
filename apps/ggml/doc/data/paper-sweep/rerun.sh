#!/bin/bash
# Re-run the cases below 0.97 (analyze.py's held-out G/H) of run dir $1,
# appending to $1/rerun.csv; analyze.py then keeps each row's minimum.
set -u
R=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
B=$R/build/apps/ggml/ggml-quant-bench
D=$(cd "$1" && pwd); A=$(dirname "$0")/../analyze.py
python3 $A $D/*.csv --cases | awk '$1+0 < 0.97 && NF > 5 {print $3, $4, $5}' | sort -u |
while read case T op; do
  IFS=/ read w a K N M <<< "$case"
  shape=$([ $op = vec_dot ] && echo ${K}x1x1 || echo ${K}x${N}x${M})
  echo "# rerun $w $op $shape ${T#T}" >> $D/rerun.err
  out=$($B --op $op --types $w --shapes $shape --threads ${T#T} --rounds 25 --min-ms 30 --providers '!cuda' 2>> $D/rerun.err)
  if [ -s $D/rerun.csv ]; then echo "$out" | grep -v '^op,' >> $D/rerun.csv; else echo "$out" > $D/rerun.csv; fi
done
