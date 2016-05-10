#!/bin/bash


export ANDROID_NDK_HOME=/local/mnt/workspace/dev/sdk/2.0/Hexagon_SDK/2.0/tools/android-ndk-r10d
export ANDROID_ARM64_TOOLCHAIN=/local/mnt/workspace/dev/sdk/2.0/Hexagon_SDK/2.0/tools/android-ndk-r10d/platforms/android-21/arch-arm64

export HEX_TOOLS=/prj/dsp/qdsp6/release/internal/branch-8.0/linux64/latest/Tools/


#make clean run-arm-64-android CXX=clang++ LDFLAGS="-L/pkg/qct/software/llvm/build_tools/libedit_tw/lib -L ../../bin -Wl,--start-group -lHalide  -lpthread -ldl -Wl,--end-group" 
make clean run-arm-64-android


