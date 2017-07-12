#!/bin/bash
export HL_TRACE_FILE=/dev/stdout
export HL_NUM_THREADS=4
make bin/process_viz && \
./bin/process_viz ../images/rgb_small.png 4 1 1 0 ./bin/out_small.png | \
../../bin/HalideTraceViz \
--size 1920 1080 --timestep 3000 \
--rgb 2 --max 65535 --strides 1 0 0 1 0 0 \
--move 30 100 --func input \
--move 1700 32 --label local_laplacian "output" 10 \
--move 1700 100 --func local_laplacian \
--gray --max 1 --strides 1 0 0 1 \
--move 280 32 --label gray "input pyramid" 10 \
--move 370 100 --store 5  --func gray \
--down 470  --store 4  --func inGPyramid[1] \
--down 250  --store 8  --func inGPyramid[2] \
--down 125  --store 16 --func inGPyramid[3] \
--down 62   --store 32 --func inGPyramid[4] \
--down 31   --store 64 --func inGPyramid[5] \
--move 680 32 --label gPyramid[1] "differently-curved intermediate pyramids" 10 \
--strides 1 0 0 1 200 0 \
--move 720 100 --store 1 --func gPyramid[0] \
--down 470  --store 2  --func gPyramid[1] \
--down 250  --store 4  --func gPyramid[2] \
--down 125  --store 8  --func gPyramid[3] \
--down 62   --store 16 --func gPyramid[4] \
--down 31   --store 32 --func gPyramid[5] \
--move 1500 32 --label outGPyramid[5] "output pyramid" 10 \
--strides 1 0 0 1 \
--move 1500 100 --store 10 --func outGPyramid[0] \
--down 470  --store 20  --func outGPyramid[1] \
--down 250  --store 40  --func outGPyramid[2] \
--down 125  --store 80  --func outGPyramid[3] \
--down 62   --store 160 --func outGPyramid[4] \
--down 31   --store 320 --func outGPyramid[5] |\
avconv -f rawvideo -pix_fmt bgr32 -s 1920x1080 -i /dev/stdin -c:v h264 ./bin/local_laplacian.mp4
#mplayer -demuxer rawvideo -rawvideo w=1920:h=1080:format=rgba:fps=30 -idle -fixed-vo -
