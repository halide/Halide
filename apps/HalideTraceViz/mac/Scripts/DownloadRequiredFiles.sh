#!/bin/bash

# DownloadRequiredFiles.sh
# mPerpetuo, inc.
# Jaime Rios

set -e
set -u

function DownloadFFMPEG()
{
    cd ..

    if [[ ! -d components/ffmpeg ]]; then
        echo "components/ffmpeg folder not found; attempting to create"
        mkdir -pv components/ffmpeg
    fi

    cd components/ffmpeg

    echo "Attempting to download ffmpeg"
    curl -L -O http://evermeet.cx/ffmpeg/ffmpeg-3.0.2.7z

    echo "Don't forget to decompress ffmpeg-3.0.2.7z prior to running Visualize script"

    cd $CURRENT_PATH
}

function DownloadHalideDistro()
{
    cd ../components

    echo "Attempting to download ffmpeg"
    curl -L -O https://github.com/halide/Halide/releases/download/release_2016_04_27/halide-mac-64-trunk-2f11b9fce62f596e832907b82d87e8f75c53dd07.tgz

    echo "Decompressing Halide distro"
    tar -xvzf halide-mac-64-trunk-2f11b9fce62f596e832907b82d87e8f75c53dd07.tgz 

    cd $CURRENT_PATH
}

echo "Starting ${0##*/}"

CURRENT_PATH=$PWD

DownloadFFMPEG
DownloadHalideDistro

echo "Script ${0##*/} done"