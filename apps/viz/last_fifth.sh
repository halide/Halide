#!/bin/bash

LINES=`wc -l $1 | cut -d' ' -f1`
LINES=$(($LINES/5))
tail -n $LINES $1