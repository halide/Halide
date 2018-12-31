#!/bin/bash

# This script should be run on boot on the arm workers

cd ~/Halide
git reset --hard HEAD
git checkout standalone_autoscheduler_arm_worker
git pull
make -j4
cd apps/random_pipeline
make clean
make

chmod a+x *.sh

bash autotune_loop_arm.sh
 
