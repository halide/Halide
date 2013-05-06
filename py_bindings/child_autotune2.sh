#!/bin/bash
# Run a schedule that outputs a bad image, compares against the reference image output by child_autotune.sh, and display an error.
HL_NUMTHREADS=8 python autotune.py autotune_compile_child examples.blur.filter_func "blur_x.chunk(x)\nblur_y.root().vectorize(x,16)" $(PWD)/apollo3.png 5 ./tune/f_fail 0 "$(PWD)/tune/f_human.png"
HL_NUMTHREADS=8 python autotune.py autotune_run_child examples.blur.filter_func "blur_x.chunk(x)\nblur_y.root().vectorize(x,16)" $(PWD)/apollo3.png 5 ./tune/f_fail 0 "$(PWD)/tune/f_human.png"
