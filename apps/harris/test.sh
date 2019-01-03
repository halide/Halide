#!/usr/bin/env bash
PROCESS="./filter_$1"
OMP_NUM_THREADS=$2 HL_NUM_THREADS=$2 ${PROCESS} ../images/rgb.png out.png
