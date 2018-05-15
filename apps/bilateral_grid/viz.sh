#!/bin/bash
echo HL_AVCONV is ${HL_AVCONV}
export HL_TRACE_FILE=/dev/stdout
export HL_NUMTHREADS=4
rm -f $1/bilateral_grid.mp4
make $1/filter_viz && \
$1/filter_viz ../images/gray_small.png $1/out_small.png 0.2 0 | \
../../bin/HalideTraceViz --size 1920 1080 | \
${HL_AVCONV} -f rawvideo -pix_fmt bgr32 -s 1920x1080 -i /dev/stdin -c:v h264 $1/bilateral_grid.mp4
#mplayer -demuxer rawvideo -rawvideo w=1920:h=1080:format=rgba:fps=30 -idle -fixed-vo -
