#!/bin/bash

export ANDROID_NDK_HOME=/local/mnt2/ronl/SDK/Hexagon_SDK/2.0/tools/android-ndk-r10d/; 

export ANDROID_ARM64_TOOLCHAIN=/local/mnt2/ronl/SDK/Hexagon_SDK/2.0/tools/android-ndk-r10d/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64
export ANDROID_ARM64_TOOLCHAIN=/local/mnt2/ronl/SDK/Hexagon_SDK/2.0/tools/android-ndk-r10d/platforms/android-21/arch-arm64

export HEX_TOOLS=/prj/dsp/qdsp6/release/internal/branch-8.0/linux64/latest/Tools/

#export TOP=`pwd`
#export LLVM_CONFIG=$TOP/../llvm/build/bin/llvm-config

make clean run-host # CXX=clang++ LDFLAGS="-L/pkg/qct/software/llvm/build_tools/libedit_tw/lib -L ../../bin -Wl,--start-group -lHalide  -lpthread -ldl -Wl,--end-group" # LIB_HALIDE=../../bin/libHalide.so  
