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

# Let the ftp server know we've started
IP=$( ifconfig eth0 | grep 192.168 | cut -d: -f2 | cut -d' ' -f1 )
touch ___started.${IP}.txt
./ftp_up.sh ___started.${IP}.txt
rm ___started.${IP}.txt

bash bench_arm.sh
