#!/bin/bash
# CPU sweep: 4096^2, M = 1..512, 12 threads and 1, every format and provider.
# usage: run.sh [build/apps/ggml dir]  (run from the repo root)
set -u
B=${1:-build/apps/ggml}/ggml-quant-bench
O=$(cd "$(dirname "$0")" && pwd)
run() { local n=$1; shift; echo "# $(git rev-parse --short HEAD) $(date -Iseconds) $*" >> $O/$n.err
  if [ -s $O/$n.csv ]; then $B "$@" 2>> $O/$n.err | grep -v '^op,' >> $O/$n.csv; else $B "$@" > $O/$n.csv 2>> $O/$n.err; fi; }
{ uname -srmo; lscpu | grep -E "Model name|^CPU\(s\)|Thread|Core|Socket|MHz|^L[123]"; nvidia-smi -L 2>/dev/null | sed "s/ (UUID.*//"; } > $O/machine.txt
for t in 12 1; do for m in 1 8 16 32 64 128 256 512; do
  run mul_mat_t$t --op mul_mat --shapes 4096x4096x$m --threads $t --early-stop --providers '!cuda'
done; done
echo done
