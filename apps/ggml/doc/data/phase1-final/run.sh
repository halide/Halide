#!/bin/bash
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
B=build/apps/ggml/ggml-quant-bench
O=$(cd "$(dirname "$0")" && pwd)
run() { local n=$1; shift; echo "# $(git rev-parse --short HEAD) $(date -Iseconds) $*" >> $O/$n.err
  if [ -s $O/$n.csv ]; then $B "$@" 2>> $O/$n.err | grep -v '^op,' >> $O/$n.csv; else $B "$@" >> $O/$n.csv 2>> $O/$n.err; fi; }
$B --check > $O/check.txt 2>&1; tail -1 $O/check.txt
run vec_dot_t1 --op vec_dot --shapes full --threads 1 --early-stop
for s in 4096x4096 4096x1024 4096x14336 14336x4096; do run gemm_t12 --op mul_mat --shapes ${s}x32 --threads 12 --early-stop; done
echo done
