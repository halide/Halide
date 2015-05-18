#!/bin/bash
export HL_TRACE=3
export HL_TRACE_FILE=/dev/stdout
export HL_NUMTHREADS=4
make process_viz && \
./process_viz ../images/rgb_small.png 4 1 1 0 out_small.png | \
../../bin/HalideTraceViz -t 3000 \
-f input           0 65535 2 1 1     30 100 1 0 0 1 0 0 \
-f gray            0 1    -1 1 5    370 100 1 0 0 1 \
-f inGPyramid[1]   0 1    -1 1 4    370 570 1 0 0 1 \
-f inGPyramid[2]   0 1    -1 1 8    370 820 1 0 0 1 \
-f inGPyramid[3]   0 1    -1 1 16   370 945 1 0 0 1 \
-f inGPyramid[4]   0 1    -1 1 32   370 1007 1 0 0 1 \
-f inGPyramid[5]   0 1    -1 1 64   370 1038 1 0 0 1 \
-f gPyramid[0]     0 1    -1 1 1    720 10 1 0 0 1 200 0 \
-f gPyramid[1]     0 1    -1 1 2    720 570 1 0 0 1 200 0 \
-f gPyramid[2]     0 1    -1 1 4    720 820 1 0 0 1 200 0 \
-f gPyramid[3]     0 1    -1 1 8    720 945 1 0 0 1 200 0 \
-f gPyramid[4]     0 1    -1 1 16   720 1007 1 0 0 1 200 0 \
-f gPyramid[5]     0 1    -1 1 32   720 1038 1 0 0 1 200 0 \
-f outGPyramid[0]  0 1    -1 1 10  1500 100 1 0 0 1 \
-f outGPyramid[1]  0 1    -1 1 20  1500 570 1 0 0 1 \
-f outGPyramid[2]  0 1    -1 1 40  1500 820 1 0 0 1 \
-f outGPyramid[3]  0 1    -1 1 80  1500 945 1 0 0 1 \
-f outGPyramid[4]  0 1    -1 1 160 1500 1007 1 0 0 1 \
-f outGPyramid[5]  0 1    -1 1 320 1500 1038 1 0 0 1 \
-f local_laplacian 0 65535 2 1 4   1700 100 1 0 0 1 0 0 |\
avconv -f rawvideo -pix_fmt bgr32 -s 1920x1080 -i /dev/stdin -c:v h264 local_laplacian.avi
#mplayer -demuxer rawvideo -rawvideo w=1920:h=1080:format=rgba:fps=30 -idle -fixed-vo -
