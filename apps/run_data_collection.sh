#!/bin/bash

ROUND=constant_bounds
APP_LIST="conv_layer interpolate iir_blur harris unsharp stencil_chain bgu camera_pipe max_filter nl_means lens_blur bilateral_grid local_laplacian"

OUTFILE=$1
OUTFILE=$(pwd)/$1


build_code() {
    echo $(pwd) $1 >> $OUTFILE;
    make clean >> $OUTFILE;
    make test &> temp;
    cat temp >> $OUTFILE;
    rm temp;
}


collect_data() {
    for i in {1..10}
    do
      build_code;
    done
}


touch $OUTFILE

for value in $APP_LIST
do
    echo $value
    cd $value
    collect_data;
    echo $(pwd) "Finished"
    cd ..
done

