#!/bin/bash
export HL_TRACE_FILE=/dev/stdout
export HL_NUM_THREADS=4
rm -f bin/local_laplacian.mp4
make bin/process_viz && \
./bin/process_viz ../images/rgb_small.png 4 1 1 0 ./bin/out_small.png | \
../../bin/HalideTraceViz \
--size 1920 1080 --timestep 3000 |\
avconv -f rawvideo -pix_fmt bgr32 -s 1920x1080 -i /dev/stdin -c:v h264 ./bin/local_laplacian.mp4
#mplayer -demuxer rawvideo -rawvideo w=1920:h=1080:format=rgba:fps=30 -idle -fixed-vo -
