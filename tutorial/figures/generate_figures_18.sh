#!/bin/bash
# This script generates the figures for lesson 18

make -C ../.. bin/HalideTraceViz

rm -rf tmp
mkdir -p tmp

# Grab a trace
HL_JIT_TARGET=host-trace_loads-trace_stores-trace_realizations \
HL_TRACE_FILE=$(pwd)/tmp/trace.bin \
make -C ../.. tutorial_lesson_18_parallel_associative_reductions
ls tmp/trace.bin

rm -rf lesson_18_*.mp4

# Serial histogram
cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 230 280 --timestep 1 --decay 2 4 --hold 10 --uninit 50 50 100 \
--max 256 --gray --strides 1 0 0 1 --zoom 20 --store 2 \
--move 10 40 --func 'hist_serial:input'	--up 8 --label 'hist_serial:input' Input 1 \
--max 25 --strides 1 0 \
--move 10 246 --func 'hist_serial' --up 8 --label 'hist_serial' Histogram 1 \
| avconv -f rawvideo -pix_fmt bgr32 -s 230x280 -i /dev/stdin -c:v h264 lesson_18_hist_serial.mp4

# Manually-factored parallel histogram
cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 460 300 --timestep 1 --decay 2 4 --hold 10 --uninit 50 50 100 \
--max 256 --gray --strides 1 0 0 1 --zoom 20 --store 2 \
--move 20 40 --func 'merge_par_manual:input' --up 8 --label 'merge_par_manual:input' Input 1 \
--max 4 \
--move 230 40 --func 'intm_par_manual' --up 8 --label 'intm_par_manual' 'Partial Histograms' 1 \
--strides 1 0 --max 10 \
--move 230 246 --func 'merge_par_manual' --up 8 --label 'merge_par_manual' Histogram 1 \
| avconv -f rawvideo -pix_fmt bgr32 -s 460x300 -i /dev/stdin -c:v h264 lesson_18_hist_manual_par.mp4

# Parallelize the outer dimension using rfactor
cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 460 300 --timestep 1 --decay 2 4 --hold 10 --uninit 50 50 100 \
--strides 1 0 0 1 --zoom 20 --gray --max 256 --store 2 \
--move 20 40 --func 'hist_rfactor_par:input' --up 8 --label 'hist_rfactor_par:input' Input 1 \
--max 4 \
--move 230 40 --func 'hist_rfactor_par_intm' --up 8 --label 'hist_rfactor_par_intm' 'Partial Histograms' 1 \
--strides 1 0 --max 10 \
--move 230 246 --func 'hist_rfactor_par' --up 8 --label 'hist_rfactor_par' Histogram 1 \
| avconv -f rawvideo -pix_fmt bgr32 -s 460x300 -i /dev/stdin -c:v h264 lesson_18_hist_rfactor_par.mp4

# Vectorize the inner dimension using rfactor
cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 460 300 --timestep 1 --decay 2 4 --hold 10 --uninit 50 50 100 \
--strides 1 0 0 1 --zoom 20 --gray --max 256 --store 2 \
--move 20 40 --func 'hist_rfactor_vec:input' --up 8 --label 'hist_rfactor_vec:input' Input 1 \
--max 4 \
--move 230 40 --func 'hist_rfactor_vec_intm' --up 8 --label 'hist_rfactor_vec_intm' 'Partial Histograms' 1 \
--strides 1 0 --max 10 \
--move 230 246 --func 'hist_rfactor_vec' --up 8 --label 'hist_rfactor_vec' Histogram 1 \
| avconv -f rawvideo -pix_fmt bgr32 -s 460x300 -i /dev/stdin -c:v h264 lesson_18_hist_rfactor_vec.mp4

# Tile histogram using rfactor
cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 650 200 --timestep 1 --decay 2 4 --hold 10 --uninit 50 50 100 \
--strides 1 0 0 1 --zoom 20 --gray --max 256 --store 2 \
--move 20 40 --func 'hist_rfactor_tile:input' --up 8 --label 'hist_rfactor_tile:input' Input 1 \
--max 4 --strides 1 0 11 0 0 2 \
--move 230 40 --func 'hist_rfactor_tile_intm' --up 8 --label 'hist_rfactor_tile_intm' 'Partial Histograms' 1 \
--strides 1 0 --max 10 \
--move 230 158 --func 'hist_rfactor_tile' --up 8 --label 'hist_rfactor_tile' Histogram 1 \
| avconv -f rawvideo -pix_fmt bgr32 -s 650x200 -i /dev/stdin -c:v h264 lesson_18_hist_rfactor_tile.mp4

rm -rf tmp
