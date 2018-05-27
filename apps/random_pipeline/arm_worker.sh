#!/bin/bash

# This script should be run on boot on the arm workers

cd ~/Halide
git reset --hard HEAD
git checkout new_autoschedule_with_new_simplifier
git pull
make -j4
cd apps/random_pipeline
make clean
make

bash bench_arm.sh
