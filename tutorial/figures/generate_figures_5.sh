#!/bin/bash
# This script generates the figures for lesson 5

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
HL_JIT_TARGET=host-trace_stores-trace_loads-trace_realizations HL_TRACE_FILE=$(pwd)/tmp/trace.bin make -C ../.. tutorial_lesson_05_scheduling_1
ls tmp/trace.bin

# row major
cat tmp/trace.bin | ../../bin/HalideTraceViz --size 192 192 --timestep 1 -d 10000 -h 4 -f gradient 0 6 -1 0 32 2 32 32 1 0 0 1 | avconv -f rawvideo -pix_fmt bgr32 -s 192x192 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_05_row_major.gif 10

# col maj
cat tmp/trace.bin | \
../../bin/HalideTraceViz --size 192 192 --timestep 1 --decay 256 256 --hold 4 \
--strides 1 0 0 1 --zoom 32 --max 6 --gray --move 32 32 \
--func gradient_col_major | \
avconv -f rawvideo -pix_fmt bgr32 -s 192x192 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_05_col_major.gif 10

# vectors
cat tmp/trace.bin | \
../../bin/HalideTraceViz --size 320 192 --timestep 1 --decay 256 256 --hold 4 \
--strides 1 0 0 1 --zoom 32 --max 11 --gray --move 32 32 \
--func gradient_in_vectors | \
avconv -f rawvideo -pix_fmt bgr32 -s 320x192 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_05_vectors.gif 10

# size-7 with a split of size 3
cat tmp/trace.bin | \
../../bin/HalideTraceViz --size 288 128 --timestep 1 --decay 256 256 --hold 4 \
--strides 1 0 0 1 --zoom 32 --max 9 --gray --store 2 --move 32 32 \
--func gradient_split_7x2 | \
avconv -f rawvideo -pix_fmt bgr32 -s 288x128 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_05_split_7_by_3.gif 10

# tiles
cat tmp/trace.bin | \
../../bin/HalideTraceViz --size 320 320 --timestep 1 --decay 256 256 --hold 10 \
--strides 1 0 0 1 --zoom 32 --max 14 --gray --move 32 32 \
--func gradient_tiled | \
 avconv -f rawvideo -pix_fmt bgr32 -s 320x320 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_05_tiled.gif 8

# fused parallel tiles
cat tmp/trace.bin | \
../../bin/HalideTraceViz --size 320 320 --timestep 1 --decay 256 256 --hold 4 \
--strides 1 0 0 1 --zoom 32 --max 14 --gray --move 32 32 \
--func gradient_fused_tiles | \
 avconv -f rawvideo -pix_fmt bgr32 -s 320x320 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_05_parallel_tiles.gif 8

# fused parallel tiles
rm -f figures/lesson_05_fast.mp4
cat tmp/trace.bin | \
../../bin/HalideTraceViz --size 700 500 --timestep 1000 --decay 1 2 --hold 30 \
--strides 1 0 0 1 --zoom 1 --max 600 --store 2 --gray --move 1 1 \
--func gradient_fast | \
avconv -f rawvideo -pix_fmt bgr32 -s 700x500 -i /dev/stdin -c:v h264 lesson_05_fast.mp4

rm -rf tmp
