#!/bin/bash
# This should run a schedule that outputs a bad image against the reference image output by child_autotune and display an error.
HL_NUMTHREADS=4 python autotune.py autotune_compile_child examples.blur.filter_func "blur_x_blurUInt16.chunk(x_blurUInt16)\nblur_y_blurUInt16.root().vectorize(x_blurUInt16,16)" $(PWD)/apollo3.png 5 ./tune/f_fail 0 "$(PWD)/tune/f_human.png"
HL_NUMTHREADS=4 python autotune.py autotune_run_child examples.blur.filter_func "blur_x_blurUInt16.chunk(x_blurUInt16)\nblur_y_blurUInt16.root().vectorize(x_blurUInt16,16)" $(PWD)/apollo3.png 5 ./tune/f_fail 0 "$(PWD)/tune/f_human.png"
