#!/bin/bash

echo "Allocations further in than the block level:"
find samples | grep "compile_log_stderr.txt\$" | xargs awk '{match($0, /^ *realize/); if (RLENGTH > 8) printf("%s:%d %s\n", FILENAME, NR, $0);}'

