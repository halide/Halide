#!/bin/bash

cd "$(dirname ${BASH_SOURCE[0]})"
../run_baremetal.sh build/bin/add_filter ../../images/gray_small.pgm 16 build/out.pgm
