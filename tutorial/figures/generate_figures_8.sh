#!/bin/bash
# This script generates the figures for lesson 8

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
HL_JIT_TARGET=host-trace_all HL_TRACE_FILE=$(pwd)/tmp/trace.bin make -C ../.. tutorial_lesson_08_scheduling_2
ls tmp/trace.bin

cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 400 236 --timestep 1 --decay 256 256 --hold 4 \
--min -1 --max 1 --gray --strides 1 0 0 1 --zoom 32 \
--move 32 48 --func producer_root \
--move 32 40 --label producer_root producer 1 \
--move 240 64 --func consumer_root \
--move 240 40 --label consumer_root consumer 1 \
| avconv -f rawvideo -pix_fmt bgr32 -s 400x236 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_08_compute_root.gif 15

cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 400 236 --timestep 1 --decay 256 256 --hold 4 \
--min -1 --max 1 --gray --strides 1 0 0 1 --zoom 32 \
--move 32 48 --func producer_y \
--move 32 40 --label producer_y producer 1 \
--move 240 64 --func consumer_y \
--move 240 40 --label consumer_y consumer 1 \
| avconv -f rawvideo -pix_fmt bgr32 -s 400x236 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_08_compute_y.gif 20

cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 400 236 --timestep 1 --decay 256 256 --hold 4 \
--min -1 --max 1 --gray --strides 1 0 0 1 --zoom 32 \
--move 32 48 --func producer_root_y \
--move 32 40 --label producer_root_y producer 1 \
--move 240 64 --func consumer_root_y \
--move 240 40 --label consumer_root_y consumer 1 \
| avconv -f rawvideo -pix_fmt bgr32 -s 400x236 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_08_store_root_compute_y.gif 20

cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 400 236 --timestep 1 --decay 256 256 --hold 4 \
--min -1 --max 1 --gray --strides 1 0 0 1 --zoom 32 \
--move 32 48 --func producer_root_x \
--move 32 40 --label producer_root_x producer 1 \
--move 240 64 --func consumer_root_x \
--move 240 40 --label consumer_root_x consumer 1 \
| avconv -f rawvideo -pix_fmt bgr32 -s 400x236 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_08_store_root_compute_x.gif 20

cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 400 236 --timestep 1 --decay 256 256 --hold 4 \
--min -1 --max 1 --gray --strides 1 0 0 1 --zoom 32 \
--move 32 48 --func producer_tile \
--move 32 40 --label producer_tile producer 1 \
--move 240 64 --func consumer_tile \
--move 240 40 --label consumer_tile consumer 1 \
| avconv -f rawvideo -pix_fmt bgr32 -s 400x236 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_08_tile.gif 10

rm -f lesson_08_mixed.mp4

cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 800 400 --timestep 200 --decay 1 3 --hold 30 \
--min -1.5 --max 1.5 --gray --strides 1 0 0 1 --zoom 2 \
--move 40 48 --func producer_mixed \
--move 40 40 --label producer_mixed producer 1 \
--move 440 48 --func consumer_mixed \
--move 440 40 --label consumer_mixed consumer 1 \
| avconv -f rawvideo -pix_fmt bgr32 -s 800x400 -i /dev/stdin -c:v h264 lesson_08_mixed.mp4
