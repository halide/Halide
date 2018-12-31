#!/bin/bash

make clean

macro=0
for ((block=0;block<16;block++)); do
    if [ $block -eq 3 ] || [ $block -eq 7 ] || [ $block -eq 13 ] 
    then
      ((macro=macro+1))
    fi 
    echo "MACRO_BLOCK_ID=$macro BLOCK_ID=$block make block"
    MACRO_BLOCK_ID=$macro BLOCK_ID=$block make block
done
