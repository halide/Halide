#!/bin/bash
# This script generates the figures for lesson 19

make -C ../.. bin/HalideTraceViz

rm -rf tmp
mkdir -p tmp

# Grab a trace
HL_JIT_TARGET=host-trace_all \
HL_TRACE_FILE=$(pwd)/tmp/trace.bin \
make -C ../.. tutorial_lesson_19_staging_func_or_image_param
ls tmp/trace.bin

rm lesson_19_*.mp4

# Local wrapper
cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 412 172 --timestep 1 --decay 1 256 --hold 20 --store 2 \
--max 16 --gray --strides 1 0 0 1 --zoom 20 \
--move 24 48 --func 'g_local:f_local' --up 8 --label 'g_local:f_local' f 1 --down 8 \
--right 132 --func 'g_local:f_local_in_g_local' --up 8 --label 'g_local:f_local_in_g_local' 'f.in(g)' 5 --down 8 \
--right 132 --func 'g_local' --up 8 --label 'g_local' g 1 --down 8 \
| avconv -f rawvideo -pix_fmt bgr32 -s 412x172 -i /dev/stdin -c:v h264 lesson_19_wrapper_local.mp4


# Global wrapper
cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 544 172 --timestep 1 --decay 1 256 --hold 20 --store 2 \
--max 16 --gray --strides 1 0 0 1 --zoom 20 \
--move 24 48 --func 'h_global:f_global' --up 8 --label 'h_global:f_global' f 1 --down 8 \
--right 132 --func 'h_global:f_global_global_wrapper' --up 8 --label 'h_global:f_global_global_wrapper' 'f.in()' 5 --down 8 \
--right 132 --func 'h_global:g_global' --up 8 --label 'h_global:g_global' g 1 --down 8 \
--right 132 --func 'h_global' --up 8 --label 'h_global' h 1 --down 8 \
| avconv -f rawvideo -pix_fmt bgr32 -s 544x172 -i /dev/stdin -c:v h264 lesson_19_wrapper_global.mp4

# Unique wrapper
cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 412 312 --timestep 1 --decay 1 256 --hold 20 --store 2 \
--max 20 --gray --strides 1 0 0 1 --zoom 20 \
--move 24 48 --down 66 --func 'h_unique:f_unique' --up 8 --label 'h_unique:f_unique' f 1 --down 8 --up 66 \
--right 132 --func 'h_unique:f_unique_in_g_unique' --up 8 --label 'h_unique:f_unique_in_g_unique' 'f.in(g)' 5 --down 8 \
--right 132 --func 'h_unique:f_unique_in_h_unique' --up 8 --label 'h_unique:f_unique_in_h_unique' 'f.in(h)' 5 --down 8 \
--down 140 --left 132 --func 'h_unique:g_unique' --up 8 --label 'h_unique:g_unique' g 1 --down 8 \
--right 132 --func 'h_unique' --up 8 --label 'h_unique' h 1 --down 8 \
| avconv -f rawvideo -pix_fmt bgr32 -s 412x312 -i /dev/stdin -c:v h264 lesson_19_wrapper_unique.mp4

# Vary schedule
cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 336 368 --timestep 1 --decay 2 256 --hold 20 --store 2 \
--max 20 --gray --strides 1 0 0 1 --zoom 16 \
--move 24 48 --func 'h_sched:f_sched_in_g_sched' --up 8 --label 'h_sched:f_sched_in_g_sched' 'f.in(g)' 1 --down 8 \
--right 160 --push --left 1488 --down 1392 --func 'h_sched:f_sched_in_h_sched' --pop \
--up 8 --label 'h_sched:f_sched_in_h_sched' 'f.in(h)' 5 --down 8 \
--down 168 --left 160 --func 'h_sched:g_sched' --up 8 --label 'h_sched:g_sched' g 1 --down 8 \
--right 160 --func 'h_sched:h_sched' --up 8 --label 'h_sched:h_sched' h 1 --down 8 \
| avconv -f rawvideo -pix_fmt bgr32 -s 336x368 -i /dev/stdin -c:v h264 lesson_19_wrapper_vary_schedule.mp4

# Transpose
cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 592 232 --timestep 1 --decay 1 256 --hold 20 --store 0 \
--min -1.1 --max 1.1 --gray --strides 1 0 0 1 --zoom 10 \
--move 24 48 --func 'g_transpose:f_transpose' --up 8 --label 'g_transpose:f_transpose' f 1 --down 8 \
--store 1 \
--blank --right 192 --func 'g_transpose:f_transpose_in_g_transpose' --up 8 --label 'g_transpose:f_transpose_in_g_transpose' 'f.in(g)' 5 --down 8 \
--no-blank --right 192 --func 'g_transpose' --up 8 --label 'g_transpose' g 1 --down 8 \
| avconv -f rawvideo -pix_fmt bgr32 -s 592x232 -i /dev/stdin -c:v h264 lesson_19_transpose.mp4

# Group updates
cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 496 200 --timestep 1 --decay 1.5 256 --hold 30 --store 2 \
--min -0.9 --max 0.9 --gray --strides 1 0 0 1 --zoom 16 \
--move 24 48 --blank --func 'g_group:f_group' --up 8 --label 'g_group:f_group' f 1 --down 8 \
--right 160 --no-blank --func 'g_group:f_group_in_g_group' --up 8 --label 'g_group:f_group_in_g_group' 'f.in(g)' 1 --down 8 \
--right 160 --func 'g_group' --up 8 --label 'g_group' g 1 --down 8 \
| avconv -f rawvideo -pix_fmt bgr32 -s 496x200 -i /dev/stdin -c:v h264 lesson_19_group_updates.mp4

rm -rf tmp
