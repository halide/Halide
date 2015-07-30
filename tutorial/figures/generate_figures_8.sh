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
HL_TRACE=3 HL_TRACE_FILE=$(pwd)/tmp/trace.bin make -C ../.. tutorial_lesson_08_scheduling_2
ls tmp/trace.bin

cat tmp/trace.bin | ../../bin/HalideTraceViz -s 400 236 -t 1 -d 10000 -h 4 -f producer_root -1 1 -1 0 32 1 32 48 1 0 0 1 -f consumer_root -1 1 -1 0 32 1 240 64 1 0 0 1 -l producer_root producer 32 40 1 -l consumer_root consumer 240 40 1 |  avconv -f rawvideo -pix_fmt bgr32 -s 400x236 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_08_compute_root.gif 15

cat tmp/trace.bin | ../../bin/HalideTraceViz -s 400 236 -t 1 -d 10000 -h 4 -f producer_y -1 1 -1 1 32 1 32 48 1 0 0 1 -f consumer_y -1 1 -1 0 32 1 240 64 1 0 0 1 -l producer_y producer 32 40 1 -l consumer_y consumer 240 40 1 |  avconv -f rawvideo -pix_fmt bgr32 -s 400x236 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_08_compute_y.gif 20

cat tmp/trace.bin | ../../bin/HalideTraceViz -s 400 236 -t 1 -d 10000 -h 4 -f producer_root_y -1 1 -1 0 32 1 32 48 1 0 0 1 -f consumer_root_y -1 1 -1 0 32 1 240 64 1 0 0 1 -l producer_root_y producer 32 40 1 -l consumer_root_y consumer 240 40 1 |  avconv -f rawvideo -pix_fmt bgr32 -s 400x236 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_08_store_root_compute_y.gif 20

cat tmp/trace.bin | ../../bin/HalideTraceViz -s 400 236 -t 1 -d 10000 -h 4 -f producer_root_x -1 1 -1 0 32 1 32 48 1 0 0 1 -f consumer_root_x -1 1 -1 0 32 1 240 64 1 0 0 1 -l producer_root_x producer 32 40 1 -l consumer_root_x consumer 240 40 1 |  avconv -f rawvideo -pix_fmt bgr32 -s 400x236 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_08_store_root_compute_x.gif 20

cat tmp/trace.bin | ../../bin/HalideTraceViz -s 400 236 -t 1 -d 10000 -h 10 -f producer_tile -1 1 -1 1 16 1 32 48 1 0 0 1 -f consumer_tile -1 1 -1 0 16 1 240 64 1 0 0 1 -l producer_tile producer 32 40 1 -l consumer_tile consumer 240 40 1 |  avconv -f rawvideo -pix_fmt bgr32 -s 400x236 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_08_tile.gif 10

rm -f lesson_08_mixed.mp4
cat tmp/trace.bin | ../../bin/HalideTraceViz -s 800 400 -t 200 -d 3 -h 30 -f producer_mixed -1.5 1.5 -1 1 2 1 40 48 1 0 0 1 -f consumer_mixed -1.5 1.5 -1 0 2 1 440 48 1 0 0 1 -l producer_mixed producer 40 40 1 -l consumer_mixed consumer 440 40 1 | avconv -f rawvideo -pix_fmt bgr32 -s 800x400 -i /dev/stdin -c:v h264 lesson_08_mixed.mp4


