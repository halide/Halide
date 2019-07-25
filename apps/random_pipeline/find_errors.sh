#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Usage: $0 dir"
    exit
fi

DIR=${1}

echo "Shared memory errors:"
find ${DIR} | grep "bench.txt\$" | xargs grep "shmem:" | awk -F: '$NF > 49152 {print $1 $NF count++} END{if (count == 0) count = 0; print count " errors found (this count may be inaccurate if the samples were not run with -debug)"}'

echo

echo "Efficiency errors:"
find ${DIR} | grep "compile_err.txt" | xargs grep efficiency | awk '/efficiency:\s*/{if ($3 > 1.0) print $0 count++} END{if (count == 0) count = 0; print count " errors found"}'

echo

echo "Block errors:"
find ${DIR} | grep "compile_err.txt" | xargs grep num_blocks | awk '/num_blocks:\s*/{if ($3 < 1) print $0 count++} END{if (count == 0) count = 0; print count " errors found"}'
