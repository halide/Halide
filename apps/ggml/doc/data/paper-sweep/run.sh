#!/bin/bash
# Paper-style x86 CPU sweep (as the M3's): Llama-3-8B projection shapes KxN,
# M in {1, 8, 32, 512}, 12 threads and 1 (one thread: M = 512 at 4096x1024
# only), and vec_dot at K = 4096 and 14336 on one thread. Every format and
# CPU provider. One CSV per (op, threads); a re-run appends to rerun.csv.
# usage: run.sh [tag]   (from anywhere; tag: output subdirectory, default "run")
set -u
R=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
B=$R/build/apps/ggml/ggml-quant-bench
O=$(cd "$(dirname "$0")" && pwd)/${1:-run}
mkdir -p $O
FLAGS="--rounds 25 --min-ms 30 --early-stop --providers !cuda"
run() { local n=$1; shift; echo "# $(git -C $R rev-parse --short HEAD) $(date -Iseconds) $*" >> $O/$n.err
  if [ -s $O/$n.csv ]; then $B "$@" $FLAGS 2>> $O/$n.err | grep -v '^op,' >> $O/$n.csv; else $B "$@" $FLAGS > $O/$n.csv 2>> $O/$n.err; fi; }
{ uname -srmo; lscpu | grep -E "Model name|^CPU\(s\)|Thread|Core|Socket|MHz|^L[123]"; } > $O/machine.txt
run vec_dot_t1 --op vec_dot --shapes 4096x1x1 --threads 1
run vec_dot_t1 --op vec_dot --shapes 14336x1x1 --threads 1
for m in 1 8 32 512; do for s in 4096x4096 4096x1024 4096x14336 14336x4096; do
  run mul_mat_t12 --op mul_mat --shapes ${s}x$m --threads 12
  if [ $m != 512 ] || [ $s = 4096x1024 ]; then run mul_mat_t1 --op mul_mat --shapes ${s}x$m --threads 1; fi
done; done
echo done
