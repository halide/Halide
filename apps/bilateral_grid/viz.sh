#!/bin/bash
export HL_TRACE=3
export HL_TRACE_FILE=/dev/stdout
export HL_NUMTHREADS=4
rm -f bilateral_grid.avi
make clean && make filter && \
./filter ../images/gray_small.png out.small 0.2 0 | \
../../bin/HalideTraceViz -t 1000 -s 1920 1080 \
-f input      0 1      -1 0 1 1 100  300 1 0 0 1 \
-f histogram  0 32     -1 0 3 1 550  100 1 0 0 1 40 0  \
-f blurz      0 512    -1 0 3 1 550  300 1 0 0 1 40 0  \
-f blurx      0 8192   -1 0 3 1 550  500 1 0 0 1 40 0  \
-f blury      0 131072 -1 0 3 1 550  700 1 0 0 1 40 0 \
-f bilateral_grid 0 1  -1 0 1 1 1564 300 1 0 0 1 | \
avconv -f rawvideo -pix_fmt bgr32 -s 1920x1080 -i /dev/stdin -c:v h264 bilateral_grid.mp4
#mplayer -demuxer rawvideo -rawvideo w=1920:h=1080:format=rgba:fps=30 -idle -fixed-vo -
