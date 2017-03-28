#!/bin/bash
# This script generates the figures for lesson 9

make_gif()
{
for f in tmp/frames_*.tif; do convert $f ${f/tif/gif}; done
gifsicle --delay $2 --colors 256 --loop tmp/frames*.gif > tmp/$1
convert -layers Optimize tmp/$1 $1
rm tmp/frames_*if
}

make -C ../.. bin/HalideTraceViz

rm -rf tmp
mkdir -p tmp

# grab a trace
HL_JIT_TARGET=host-trace_stores-trace_loads-trace_realizations HL_TRACE_FILE=$(pwd)/tmp/trace.bin make -C ../.. tutorial_lesson_09_update_definitions
ls tmp/trace.bin

cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 192 192 --timestep 1 --decay 10000 --hold 10 \
--min 0 --max 10 --gray --zoom 32 --store 2 --strides 1 0 0 1 \
--move 32 32 --func g \
| avconv -f rawvideo -pix_fmt bgr32 -s 192x192 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_09_update.gif 15

rm -f lesson_09_update_rdom.mp4
cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 364 364 --timestep 100 --decay 2 --hold 40 \
--min 0 --max 2 --gray --zoom 3 --store 1 \
--strides 1 0 0 1 \
--move 32 32 --func 'f$1' \
| avconv -f rawvideo -pix_fmt bgr32 -s 364x364 -i /dev/stdin -c:v h264 lesson_09_update_rdom.mp4

rm -f lesson_09_update_schedule.mp4

cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 320 320 --timestep 1 --decay 2 --hold 40 \
--min -20 --max 200 --gray --zoom 16 --store 1 \
--strides 1 0 0 1 \
--move 32 32 --func 'f$2' \
| avconv -f rawvideo -pix_fmt bgr32 -s 320x320 -i /dev/stdin -c:v h264 lesson_09_update_schedule.mp4

cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 384 144 --timestep 1 --decay 10000 --hold 5 \
--min 0 --max 60 --gray --zoom 32 --store 2 --strides 1 0 \
--move 32 32 --func 'producer' \
--label producer producer 1 \
--down 64 --func 'consumer' \
--label consumer consumer 1 \
| avconv -f rawvideo -pix_fmt bgr32 -s 384x144 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_09_inline_reduction.gif 15

cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 384 144 --timestep 1 --decay 10000 --hold 5 \
--min 0 --max 340 --gray --zoom 32 --store 1 \
--strides 1 0 \
--move 32 32 --func 'producer$2' \
--label 'producer$2' producer \
--down 64 --func 'consumer$2' \
--label 'consumer$2' consumer \
| avconv -f rawvideo -pix_fmt bgr32 -s 384x144 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_09_compute_at_pure.gif 15

cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 384 144 --timestep 1 --decay 10000 --hold 5 \
--min 0 --max 200 --gray --zoom 32 --store 1 \
--strides 1 0 \
--move 32 32 --func 'producer$3' \
--label 'producer$3' producer \
--down 64 --func 'consumer$3' \
--label 'consumer$3' consumer \
| avconv -f rawvideo -pix_fmt bgr32 -s 384x144 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_09_compute_at_update.gif 15

cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 384 144 --timestep 1 --decay 10000 --hold 5 \
--min 0 --max 150 --gray --zoom 32 --store 1 \
--strides 1 0 \
--move 32 32 --func 'producer$4' \
--label 'producer$4' producer \
--down 64 --func 'consumer$4' \
--label 'consumer$4' consumer \
| avconv -f rawvideo -pix_fmt bgr32 -s 384x144 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_09_compute_at_pure_and_update.gif 15

rm -f lesson_09_compute_at_multiple_updates.mp4
cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 576 304 --timestep 1 --decay 10000 --hold 40 \
--min 0 --max 15 --gray --strides 1 0 0 1 --zoom 24 --store 1 \
--move 32 32 --func 'producer_1' \
--label 'producer_1' producer 1 \
--func 'producer_2' \
--move 304 32 --max 20 --func 'consumer_2' \
--label 'consumer_2' consumer 1 \
| avconv -f rawvideo -pix_fmt bgr32 -s 576x304 -i /dev/stdin -c:v h264 lesson_09_compute_at_multiple_updates.mp4

cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 544 144 --timestep 1 --decay 10000 --hold 5 \
--min 0 --max 16 --zoom 32 --store 1 --strides 1 0 --gray \
--move 32 32 --func 'producer$5' \
--label 'producer$5' producer 1 \
--down 64 --max 50 --func 'consumer$5' \
--label 'consumer$5' consumer 1 \
| avconv -f rawvideo -pix_fmt bgr32 -s 544x144 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_09_compute_at_rvar.gif 10
