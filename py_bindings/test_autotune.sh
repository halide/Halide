#!/bin/bash
HL_NUMTHREADS=4 python autotune.py autotune examples.blur.filter_func -cores 4 -in_image apollo3.png
