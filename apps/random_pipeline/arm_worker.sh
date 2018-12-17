#!/bin/bash

# This script should be run on boot on the arm workers

cd ~/Halide
git reset --hard HEAD
git checkout new_autoschedule_with_new_simplifier_arm_worker_branch
git pull
make -j4
cd apps/random_pipeline
make clean
make

chmod a+x *.sh

bash bench_arm.sh
