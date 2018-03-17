#!/bin/bash
export HL_TRACE_FILE=/dev/stdout
export HL_NUMTHREADS=4
rm -f $1/camera_pipe.mp4
$1/viz/process ../images/bayer_small.png 3700 1.8 50 1 1 $1/out.png |
../../bin/HalideTraceViz --timestep 1000 --size 1920 1080 \
--strides 1 0 0 1 --gray --max 1024 \
--move 1% 32% --func input \
--move 16% 33% --func denoised \
--strides 1 0 0 1 0 20% \
--move 30% 11% --func deinterleaved \
--strides 1 0 0 1 \
--move 37% 11% --func r_gr --label r_gr "r@gr" 10 \
--right 7% --func g_gr --label g_gr "g@gr" 10 \
--right 7% --func b_gr --label b_gr "b@gr" 10 \
--move 37% 31% --func r_r --label r_r "r@r" 10 \
--right 7% --func g_r --label g_r "g@r" 10 \
--right 7% --func b_r --label b_r "b@r" 10 \
--move 37% 50% --func r_b --label r_b "r@b" 10 \
--right 7% --func g_b --label g_b "g@b" 10 \
--right 7% --func b_b --label b_b "b@b" 10 \
--move 37% 80% --func r_gb --label r_gb "r@gb" 10 \
--right 7% --func g_gb --label g_gb "g@gb" 10 \
--right 7% --func b_gb --label b_gb "b@gb" 10 \
--strides 1 0 0 1 0 0 --rgb 2 \
--move 60% 33% --func output --label output "demosaiced" 10 \
--move 73% 33% --func corrected --label corrected "color-corrected" 10 \
--max 256 --move 86% 33% --func curved --label curved "gamma-corrected" 10 |\
avconv -f rawvideo -pix_fmt bgr32 -s 1920x1080 -i /dev/stdin -c:v h264 $1/camera_pipe.mp4
#mplayer -demuxer rawvideo -rawvideo w=1920:h=1080:format=rgba:fps=30 -idle -fixed-vo -
