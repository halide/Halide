FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    ln -fs /usr/share/zoneinfo/America/New_York /etc/localtime && \
    apt-get install -y tzdata && \
    apt-get install -y \
        build-essential \
        cmake \
        gdb \
        git \
        gnupg \
        libcurl4-openssl-dev \
        libedit-dev \
        libicu-dev \
        libjpeg-dev \
        libncurses-dev \
        libpng-dev \
        lsb-release \
        ninja-build \
        pkg-config \
        python3-dev \
        software-properties-common \
        wget \
        zlib1g-dev && \
    wget https://apt.llvm.org/llvm.sh && \
    chmod +x llvm.sh && \
    ./llvm.sh 21 all && \
    apt-get install -y liblld-21-dev && \
    apt-get clean && \
    rm -rf llvm.sh /var/lib/apt/lists/* \
