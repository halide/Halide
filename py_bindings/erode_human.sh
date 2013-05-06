#!/bin/bash
# This should run successfully and return a low time for the human-tuned schedule. It also writes the reference image ./tune/f_human.png
mkdir tune
HL_NUMTHREADS=8 python autotune.py autotune_compile_child examples.erode.filter_func "erode_x.root().split(y, y, _c0, 8).parallel(y)\nerode_y.root().split(y, y, _c0, 8).parallel(y)" $(PWD)/apollo3.png 5 ./tune/f_human 1 ""
HL_NUMTHREADS=8 python autotune.py autotune_run_child examples.erode.filter_func "erode_x.root().split(y, y, _c0, 8).parallel(y)\nerode_y.root().split(y, y, _c0, 8).parallel(y)" $(PWD)/apollo3.png 5 ./tune/f_human 1 ""
