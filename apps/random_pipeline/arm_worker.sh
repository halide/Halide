#!/bin/bash

# This script should be run on boot on the arm workers

cd ~/Halide
git reset --hard HEAD
git fetch
git checkout standalone_autoscheduler_arm_worker
bash apps/random_pipeline/arm_worker.sh
