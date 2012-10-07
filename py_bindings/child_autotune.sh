#!/bin/bash
# This should run successfully and return a low time for the human-tuned schedule. It also writes the reference image ./tune/f_human.png
mkdir tune
HL_NUMTHREADS=4 python autotune.py autotune_compile_child examples.blur.filter_func "blur_x.chunk(x).vectorize(x,8)\nblur_y.root().tile(x, y, _c0, _c1, 8, 8).vectorize(_c0,8).parallel(y)" $(PWD)/apollo3.png 5 ./tune/f_human 1 ""
HL_NUMTHREADS=4 python autotune.py autotune_run_child examples.blur.filter_func "blur_x.chunk(x).vectorize(x,8)\nblur_y.root().tile(x, y, _c0, _c1, 8, 8).vectorize(_c0,8).parallel(y)" $(PWD)/apollo3.png 5 ./tune/f_human 1 ""
