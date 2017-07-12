#!/bin/bash
export HL_TRACE_FILE=/dev/stdout
export HL_NUMTHREADS=4
rm -f $1/bilateral_grid.mp4
make $1/filter_viz && \
$1/filter_viz ../images/gray_small.png $1/out.small 0.2 0 | \
../../bin/HalideTraceViz --timestep 1000 --size 1920 1080 \
--gray --strides 1 0 0 1 \
--max 1 --move 100 300 --func input \
--strides 1 0 0 1 40 0 --zoom 3 \
--max 32 --move 550 100 --func histogram \
--max 512 --down 200 --func blurz \
--max 8192 --down 200 --func blurx \
--max 131072 --down 200 --func blury \
--strides 1 0 0 1 --zoom 1 \
--max 1 --move 1564 300 --func bilateral_grid | \
avconv -f rawvideo -pix_fmt bgr32 -s 1920x1080 -i /dev/stdin -c:v h264 $1/bilateral_grid.mp4
#mplayer -demuxer rawvideo -rawvideo w=1920:h=1080:format=rgba:fps=30 -idle -fixed-vo -
