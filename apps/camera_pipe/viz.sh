#!/bin/bash
export HL_TRACE_FILE=/dev/stdout
export HL_NUMTHREADS=4
rm -f $1/camera_pipe.mp4
# Do trivial partial-overrides of trace settings via flags
# (--zoom and --rlabel) just to demonstrate that it works.
$1/viz/process ../images/bayer_small.png 3700 1.8 50 1 1 $1/out.png |
../../bin/HalideTraceViz --timestep 1000 --size 1920 1080 \
--zoom 4 --func sharpen_strength_x32 \
--rlabel curve "tone curve LUT" 0 0 10 \
|\
${HL_AVCONV} -f rawvideo -pix_fmt bgr32 -s 1920x1080 -i /dev/stdin -c:v h264 $1/camera_pipe.mp4
#mplayer -demuxer rawvideo -rawvideo w=1920:h=1080:format=rgba:fps=30 -idle -fixed-vo -
