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
HL_TRACE=3 HL_TRACE_FILE=$(pwd)/tmp/trace.bin make -C ../.. tutorial_lesson_05_scheduling_1
ls tmp/trace.bin

# row major
cat tmp/trace.bin | ../../bin/HalideTraceViz -s 192 192 -t 1 -d 10000 -h 4 -f gradient 0 6 -1 32 2 32 32 1 0 0 1 | avconv -f rawvideo -pix_fmt bgr32 -s 192x192 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_05_row_major.gif 10

# col maj
cat tmp/trace.bin | ../../bin/HalideTraceViz -s 192 192 -t 1 -d 10000 -h 4 -f gradient_col_maj 0 6 -1 32 2 32 32 1 0 0 1 | avconv -f rawvideo -pix_fmt bgr32 -s 192x192 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_05_col_major.gif 10

# vectors
cat tmp/trace.bin | ../../bin/HalideTraceViz -s 320 192 -t 1 -d 10000 -h 4 -f gradient_in_vect 0 11 -1 32 1 32 32 1 0 0 1 | avconv -f rawvideo -pix_fmt bgr32 -s 320x192 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_05_vectors.gif 10

# size-7 with a split of size 3
cat tmp/trace.bin | ../../bin/HalideTraceViz -s 288 192 -t 1 -d 10000 -h 4 -f gradient_split_7 0 9 -1 32 2 32 32 1 0 0 1 | avconv -f rawvideo -pix_fmt bgr32 -s 288x192 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_05_split_7_by_3.gif 10

# tiles
cat tmp/trace.bin | ../../bin/HalideTraceViz -s 320 320 -t 1 -d 10000 -h 10 -f gradient_tiled 0 14 -1 32 1 32 32 1 0 0 1 | avconv -f rawvideo -pix_fmt bgr32 -s 320x320 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_05_tiled.gif 8

# fused parallel tiles
cat tmp/trace.bin | ../../bin/HalideTraceViz -s 320 320 -t 1 -d 10000 -h 4 -f gradient_fused_t 0 14 -1 32 1 32 32 1 0 0 1 | avconv -f rawvideo -pix_fmt bgr32 -s 320x320 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_05_parallel_tiles.gif 8

# fused parallel tiles
cat tmp/trace.bin |  ../../bin/HalideTraceViz -s 700 500 -t 1000 -d 2 -h 30 -f gradient_fast 0 600 -1 2 1 1 1 1 0 0 1 | avconv -f rawvideo -pix_fmt bgr32 -s 700x500 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_05_fast.gif 5

rm -f figures/lesson_05_fast.mp4
cat tmp/trace.bin |  ../../bin/HalideTraceViz -s 700 500 -t 1000 -d 2 -h 30 -f gradient_fast 0 600 -1 2 1 1 1 1 0 0 1 | avconv -f rawvideo -pix_fmt bgr32 -s 700x500 -i /dev/stdin -c:v h264 lesson_05_fast.mp4
