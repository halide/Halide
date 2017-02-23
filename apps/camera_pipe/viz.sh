#!/bin/bash
export HL_TRACE=3
export HL_TRACE_FILE=/dev/stdout
export HL_NUMTHREADS=4
rm -f $1/camera_pipe.avi
$1/viz/process ../images/bayer_small.png 3700 1.8 50 1 $1/out.png |
../../bin/HalideTraceViz -t 1000 -s 1920 1080 \
-f input         0 1024  -1 0 1 1 10   348 1 0 0 1 \
-f denoised      0 1024  -1 0 1 1 305  360 1 0 0 1 \
-f deinterleaved 0 1024  -1 0 1 1 580  120 1 0 0 1 0 220 \
-f r_gr          0 1024  -1 0 1 1 720  120 1 0 0 1 \
-f g_gr          0 1024  -1 0 1 1 860  120 1 0 0 1 \
-f b_gr          0 1024  -1 0 1 1 1000 120 1 0 0 1 \
-f r_r           0 1024  -1 0 1 1 720  340 1 0 0 1 \
-f g_r           0 1024  -1 0 1 1 860  340 1 0 0 1 \
-f b_r           0 1024  -1 0 1 1 1000 340 1 0 0 1 \
-f r_b           0 1024  -1 0 1 1 720  560 1 0 0 1 \
-f g_b           0 1024  -1 0 1 1 860  560 1 0 0 1 \
-f b_b           0 1024  -1 0 1 1 1000 560 1 0 0 1 \
-f r_gb          0 1024  -1 0 1 1 720  780 1 0 0 1 \
-f g_gb          0 1024  -1 0 1 1 860  780 1 0 0 1 \
-f b_gb          0 1024  -1 0 1 1 1000 780 1 0 0 1 \
-f output        0 1024   2 0 1 1 1140 360 1 0 0 1 0 0 \
-f corrected     0 1024   2 0 1 1 1400 360 1 0 0 1 0 0 \
-f curved        0 256    2 0 1 1 1660 360 1 0 0 1 0 0 | \
avconv -f rawvideo -pix_fmt bgr32 -s 1920x1080 -i /dev/stdin -c:v h264 $1/camera_pipe.avi
#mplayer -demuxer rawvideo -rawvideo w=1920:h=1080:format=rgba:fps=30 -idle -fixed-vo -
