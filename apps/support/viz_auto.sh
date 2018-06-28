#!/bin/bash
#
# $1 = filter cmd to run, including args
# $2 = HalideTraceViz executable
# $3 = path to output mp4

rm -rf "$3"

# Use a named pipe for the $1 -> HTV pipe, just in case
# the exe in $1 writes any random output to stdout.
PIPE=/tmp/halide_viz_auto_pipe
rm -rf $PIPE
mkfifo $PIPE

HL_TRACE_FILE=${PIPE} HL_NUMTHREADS=8 $1 &

$2 --auto_layout --ignore_tags 0<${PIPE} | \
${HL_AVCONV} -y -f rawvideo -pix_fmt bgr32 -s 1920x1080 -i /dev/stdin -c:v h264 "$3"
