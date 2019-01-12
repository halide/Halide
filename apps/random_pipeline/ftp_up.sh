#!/bin/bash

aws s3 cp "$1" "s3://io.halide.autoscheduler.siggraph-2019-arm/gen2/"
#echo "
#  verbose
#  open 192.168.86.250
#  user anonymous pass
#  cd samples
#  put $1
#  bye
#" | ftp -n
