#!/bin/bash
# This script generates the figures for lesson 17

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

rm lesson_17_*.mp4

# Grab a trace
HL_JIT_TARGET=host-trace_all \
HL_TRACE_FILE=$(pwd)/tmp/trace.bin \
make -C ../.. tutorial_lesson_17_predicated_rdom
ls tmp/trace.bin

# Circular rdom
cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 236 236 --timestep 1 --decay 1 256 --hold 30 \
--move 13 13 --strides 1 0 0 1 --max 20 --gray --store 2 --zoom 30 \
--func circle \
| avconv -f rawvideo -pix_fmt bgr32 -s 236x236 -i /dev/stdin -c:v h264 lesson_17_rdom_circular.mp4

# Triangular rdom
cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 236 236 --timestep 1 --decay 1 256 --hold 30 \
--move 18 18 --strides 1 0 0 1 --max 32 --gray --store 2 --zoom 20 \
--func triangle \
| avconv -f rawvideo -pix_fmt bgr32 -s 236x236 -i /dev/stdin -c:v h264 lesson_17_rdom_triangular.mp4

make_gif lesson_17_rdom_triangular.gif 4

# Rdom with calls in predicate
cat tmp/trace.bin | ../../bin/HalideTraceViz \
--size 400 236 --timestep 1 --decay 1 256 --hold 60 \
--min -1 --max 13 --gray --strides 1 0 0 1 --zoom 32 --store 3 --load 2 \
--move 20 48 --func f --move 32 40 --label f f 1 \
--move 228 48 --func g --move 240 40 --label g g 1 \
| avconv -f rawvideo -pix_fmt bgr32 -s 400x236 -i /dev/stdin -c:v h264 lesson_17_rdom_calls_in_predicate.mp4

rm -rf tmp
