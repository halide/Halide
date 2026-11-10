#!/bin/bash
# usage: rp.sh types "Ms" [threads]  - best of 2: ggml best, halide aos, halide 16x4 (q8_K acts)
cd /home/areinking/dev/Halide/ggml-quant/build/apps/ggml
for m in $2; do for p in 1 2; do ./ggml-quant-bench --op mul_mat --types $1 --shapes 4096x4096x$m --threads ${3:-12} --providers '!cuda' 2>/dev/null | awk -F, -v m=$m 'NR>1 && ($11 ~ /^ggml/ && $3=="f32" || $11 ~ /_q8_K_mul_mat$/) {print $2, m, $11, $15/1000}'; done; done | awk '{k=$1" "$2" "$3; if(!(k in b)||$4<b[k])b[k]=$4} END{for(k in b)print k,b[k]}' | sort -k1,1 -k2n,2 -k3
