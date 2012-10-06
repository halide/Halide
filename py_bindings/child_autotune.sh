#!/bin/bash
# This should run successfully and return a low time for the human-tuned schedule. It also writes the reference image ./tune/f_human.png
mkdir tune
HL_NUMTHREADS=4 python autotune.py autotune_compile_child examples.blur.filter_func "blur_x_blurUInt16.chunk(x_blurUInt16).vectorize(x_blurUInt16,8)\nblur_y_blurUInt16.root().tile(x_blurUInt16, y_blurUInt16, _c0, _c1, 8, 8).vectorize(_c0,8).parallel(y_blurUInt16)" $(PWD)/apollo3.png 5 ./tune/f_human 1 ""
HL_NUMTHREADS=4 python autotune.py autotune_run_child examples.blur.filter_func "blur_x_blurUInt16.chunk(x_blurUInt16).vectorize(x_blurUInt16,8)\nblur_y_blurUInt16.root().tile(x_blurUInt16, y_blurUInt16, _c0, _c1, 8, 8).vectorize(_c0,8).parallel(y_blurUInt16)" $(PWD)/apollo3.png 5 ./tune/f_human 1 ""
