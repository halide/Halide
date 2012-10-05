#!/bin/bash
HL_NUMTHREADS=4 python autotune.py autotune_compile_child examples.blur.filter_func "blur_x_blurUInt16.root().vectorize(x_blurUInt16,8)" $(PWD)/apollo2.png 5 12680 ./tune/f0_72
HL_NUMTHREADS=4 python autotune.py autotune_run_child examples.blur.filter_func "blur_x_blurUInt16.root().vectorize(x_blurUInt16,8)" $(PWD)/apollo2.png 5 12680 ./tune/f0_72
