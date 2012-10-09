#!/bin/bash
# This should run successfully and return a low time for the human-tuned schedule. It also writes the reference image ./tune/f_human.png
mkdir tune
HL_NUMTHREADS=8 python autotune.py autotune_compile_child examples.erode.filter_func "erode_x.chunk(y).vectorize(y,8)\nerode_y.root().tile(x,y,_c0,_c1,64,64).vectorize(_c0,16).parallel(y)" $(PWD)/apollo3.png 5 ./tune/f_autotune 1 ""
HL_NUMTHREADS=8 python autotune.py autotune_run_child examples.erode.filter_func "erode_x.chunk(y).vectorize(y,8)\nerode_y.root().tile(x,y,_c0,_c1,64,64).vectorize(_c0,16).parallel(y)" $(PWD)/apollo3.png 5 ./tune/f_autotune 1 ""
