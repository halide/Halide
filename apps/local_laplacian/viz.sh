#!/bin/bash
export HL_TRACE_FILE=/dev/stdout
export HL_NUM_THREADS=4
rm -f $1/local_laplacian.mp4
make $1/process_viz && \
./$1/process_viz ../images/rgb_small.png 4 1 1 0 ./$1/out_small.png | \
../../bin/HalideTraceViz \
--size 1920 1080 --timestep 3000 | \
${HL_AVCONV} -f rawvideo -pix_fmt bgr32 -s 1920x1080 -i /dev/stdin -c:v h264 ./$1/local_laplacian.mp4
#mplayer -demuxer rawvideo -rawvideo w=1920:h=1080:format=rgba:fps=30 -idle -fixed-vo -
