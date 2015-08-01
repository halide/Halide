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
HL_TRACE=3 HL_TRACE_FILE=$(pwd)/tmp/trace.bin make -C ../.. tutorial_lesson_09*
ls tmp/trace.bin

cat tmp/trace.bin | ../../bin/HalideTraceViz -s 192 192 -t 1 -d 10000 -h 10 -f g 0 10 -1 0 32 2 32 32 1 0 0 1  |  avconv -f rawvideo -pix_fmt bgr32 -s 192x192 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_09_update.gif 15

rm -f lesson_09_update_rdom.mp4
cat tmp/trace.bin | ../../bin/HalideTraceViz -s 364 364 -t 100 -d 2 -h 40 -f 'f$2' 0 2 -1 0 3 1 32 32 1 0 0 1  | avconv -f rawvideo -pix_fmt bgr32 -s 364x364 -i /dev/stdin -c:v h264 lesson_09_update_rdom.mp4

rm -f lesson_09_update_schedule.mp4
cat tmp/trace.bin | ../../bin/HalideTraceViz -s 320 320 -t 1 -d 2 -h 40 -f 'f$3' -20 200 -1 0 16 1 32 32 1 0 0 1   | avconv -f rawvideo -pix_fmt bgr32 -s 320x320 -i /dev/stdin -c:v h264 lesson_09_update_schedule.mp4

cat tmp/trace.bin | ../../bin/HalideTraceViz -s 384 144 -t 1 -d 10000 -h 5 -f 'producer' 0 60 -1 0 32 2 32 32 1 0 -l producer producer 32 32 1 -f 'consumer' 0 60 -1 0 32 1 32 96 1 0 -l consumer consumer 32 96 1  |  avconv -f rawvideo -pix_fmt bgr32 -s 384x144 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_09_inline_reduction.gif 15

cat tmp/trace.bin | ../../bin/HalideTraceViz -s 384 144 -t 1 -d 10000 -h 5 -f 'producer$2' 0 340 -1 0 32 1 32 32 1 0 -l 'producer$2' producer 32 32 1 -f 'consumer$2' 0 340 -1 0 32 1 32 96 1 0 -l 'consumer$2' consumer 32 96 1   |  avconv -f rawvideo -pix_fmt bgr32 -s 384x144 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_09_compute_at_pure.gif 15

cat tmp/trace.bin | ../../bin/HalideTraceViz -s 384 144 -t 1 -d 10000 -h 5 -f 'producer$3' 0 200 -1 0 32 1 32 32 1 0 -l 'producer$3' producer 32 32 1 -f 'consumer$3' 0 200 -1 0 32 1 32 96 1 0 -l 'consumer$3' consumer 32 96 1  |  avconv -f rawvideo -pix_fmt bgr32 -s 384x144 -i /dev/stdin tmp/frames_%04d.tif

make_gif lesson_09_compute_at_update.gif 15

cat tmp/trace.bin | ../../bin/HalideTraceViz -s 384 144 -t 1 -d 10000 -h 5 -f 'producer$4' 0 150 -1 0 32 1 32 32 1 0 -l 'producer$4' producer 32 32 1 -f 'consumer$4' 0 170 -1 0 32 1 32 96 1 0 -l 'consumer$4' consumer 32 96 1  |  avconv -f rawvideo -pix_fmt bgr32 -s 384x144 -i /dev/stdin tmp/frames_%04d.tif
make_gif lesson_09_compute_at_pure_and_update.gif 15

rm -f lesson_09_compute_at_multiple_updates.mp4
cat tmp/trace.bin | ../../bin/HalideTraceViz -s 576 304 -t 1 -d 10000 -h 40 -f 'producer_1' 0 15 -1 0 24 1 32 32 1 0 0 1 -l 'producer_1' producer 32 32 1 -f 'producer_2' 0 15 -1 0 24 1 32 32 1 0 0 1  -f 'consumer_2' 0 20 -1 0 24 1 304 32 1 0 0 1 -l 'consumer_2' consumer 304 32 1 | avconv -f rawvideo -pix_fmt bgr32 -s 576x304 -i /dev/stdin -c:v h264 lesson_09_compute_at_multiple_updates.mp4


cat tmp/trace.bin | ../../bin/HalideTraceViz -s 544 144 -t 1 -d 10000 -h 5 -f 'producer$6' 0 16 -1 0 32 1 32 32 1 0 -l 'producer$6' producer 32 32 1 -f 'consumer$6' 0 50 -1 0 32 1 32 96 1 0 -l 'consumer$6' consumer 32 96 1  | avconv -f rawvideo -pix_fmt bgr32 -s 544x144 -i /dev/stdin tmp/frames_%04d.tif
make_gif lesson_09_compute_at_rvar.gif 10


