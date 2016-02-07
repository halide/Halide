#!/usr/bin/env bash

for kw in 2 3 4 5 6 7 8 9 # core dumps due to expr explosion on 10x10 and up
do
    ./halide_blur_variants $kw 2> /dev/null
    echo ""
done
