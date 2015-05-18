#!/bin/bash

HEAD=`git show HEAD | head -n1 | cut -d' ' -f2`

if [[ ! -f test/scripts/test_target.sh ]]; then
    echo Could not find test/scripts/test_target.sh
    echo Run this script from the root directory of your Halide checkout
    exit 1
fi

mkdir -p llvm
mkdir -p testing/reports/${HEAD}
mkdir -p testing/deps

# Acquire and build 32-bit and 64-bit libpng and libz.  We can't trust
# the OS version to come in both 32 and 64-bit flavors, especially on
# OS X.
if [[ ! -f testing/deps/libpng32.a ]]; then
    cd testing/deps
    echo Acquiring and building libpng
    curl -L http://sourceforge.net/projects/libpng/files/libpng16/1.6.16/lpng1616.zip/download -o lpng1616.zip || exit 1
    unzip -n lpng1616.zip
    cd lpng1616
    if [[ `uname` == Darwin ]]; then
        make -f scripts/makefile.darwin clean
        make -f scripts/makefile.darwin ARCH="-arch i386 -arch x86_64" || exit 1
        cp libpng.a ../libpng32.a
        cp libpng.a ../libpng64.a
        cp *.h ../
    elif [[ `uname` == Linux ]]; then
        make -f scripts/makefile.linux clean
        make -f scripts/makefile.linux || exit 1
        cp libpng.a ../libpng64.a
        make -f scripts/makefile.linux clean
        make -f scripts/makefile.linux CC="gcc -m32" || exit 1
        cp libpng.a ../libpng32.a
        cp *.h ../
    else
        echo "Can't determine host OS"
        exit 1
    fi

    cd ../../../
fi

if [[ ! -f testing/deps/libz32.a ]]; then
    cd testing/deps
    curl http://zlib.net/zlib128.zip -o zlib128.zip || exit 1
    unzip -n zlib128.zip
    cd zlib-1.2.8
    if [[ `uname` == Darwin ]]; then
        ./configure --static --archs="-arch i386 -arch x86_64" || exit 1
        make clean
        make || exit 1
        cp libz.a ../libz32.a
        cp libz.a ../libz64.a
    elif [[ `uname` == Linux ]]; then
        ./configure --static
        make clean
        make || exit 1
        cp libz.a ../libz64.a
        make clean
        make CC="gcc -m32"
        cp libz.a ../libz32.a
    else
        echo "Can't determine host OS"
        exit 1
    fi
    cd ../../../
fi

if [[ `uname` == Darwin ]]; then
    export COMPILERS="clang++"
    export CXXFLAGS="-stdlib=libc++"
    export LLVMS="trunk release-3.5 release-3.6 pnacl"
else
    export COMPILERS="g++-4.8 g++-4.9"
    export CXXFLAGS=""
    export LD_LIBRARY_PATH=/usr/local/lib32:/usr/local/lib64
    export LLVMS="release-3.5 trunk release-3.6 pnacl"
fi

# link testing/reports/head to the current head
rm -rf testing/reports/head/*
rm -rf testing/reports/head
ln -s ${HEAD} testing/reports/head

for COMPILER in ${COMPILERS}; do
    export CXX="${COMPILER} ${CXXFLAGS}"
    export CC="${COMPILER/++/}"
    export CC="${CC/g-/gcc-}"
    echo $CXX $CC

    # test several llvm variants
    for LLVM in ${LLVMS}; do

        if [[ "$LLVM" == pnacl ]]; then
            LLVM_REPO=https://chromium.googlesource.com/native_client/pnacl-llvm.git
            CLANG_REPO=https://chromium.googlesource.com/native_client/pnacl-clang.git
            LLVM_TARGETS="X86;ARM;AArch64;NVPTX;Mips"
        elif [[ "$LLVM" == trunk ]]; then
            LLVM_REPO=http://llvm.org/svn/llvm-project/llvm/trunk
            CLANG_REPO=http://llvm.org/svn/llvm-project/cfe/trunk
            LLVM_TARGETS="X86;ARM;AArch64;NVPTX;Mips"
        elif [[ "$LLVM" == release-3.3 ]]; then
            LLVM_REPO=http://llvm.org/svn/llvm-project/llvm/branches/release_33
            CLANG_REPO=http://llvm.org/svn/llvm-project/cfe/branches/release_33
            LLVM_TARGETS="X86;ARM;AArch64;NVPTX;Mips"
        elif [[ "$LLVM" == release-3.4 ]]; then
            LLVM_REPO=http://llvm.org/svn/llvm-project/llvm/branches/release_34
            CLANG_REPO=http://llvm.org/svn/llvm-project/cfe/branches/release_34
            LLVM_TARGETS="X86;ARM;AArch64;NVPTX;Mips"
        elif [[ "$LLVM" == release-3.5 ]]; then
            LLVM_REPO=http://llvm.org/svn/llvm-project/llvm/branches/release_35
            CLANG_REPO=http://llvm.org/svn/llvm-project/cfe/branches/release_35
            LLVM_TARGETS="X86;ARM;AArch64;NVPTX;Mips"
        elif [[ "$LLVM" == release-3.6 ]]; then
            LLVM_REPO=http://llvm.org/svn/llvm-project/llvm/branches/release_36
            CLANG_REPO=http://llvm.org/svn/llvm-project/cfe/branches/release_36
            LLVM_TARGETS="X86;ARM;AArch64;NVPTX;Mips"
        fi

        # Check out llvm if necessary
        if [ ! -d llvm/${LLVM} ]; then
            if [[ "$LLVM_REPO" == *.git ]]; then
                git clone $LLVM_REPO llvm/${LLVM}
                git clone $CLANG_REPO llvm/${LLVM}/tools/clang
            else
                svn co $LLVM_REPO llvm/${LLVM}
                svn co $CLANG_REPO llvm/${LLVM}/tools/clang
            fi
        fi

        export BUILD32="build-32-${COMPILER}"
        export BUILD64="build-64-${COMPILER}"

        # Configure and build it
        cd llvm/${LLVM}
        if [ ! -f ${BUILD32}/bin/llvm-config ]; then
            mkdir ${BUILD32}
            cd ${BUILD32}
            cmake -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_TARGETS_TO_BUILD=${LLVM_TARGETS} -DLLVM_ENABLE_ASSERTIONS=ON -DCMAKE_BUILD_TYPE=Release -DLLVM_BUILD_32_BITS=ON ..
            make -j16
            cd ..
        fi
        if [ ! -f ${BUILD64}/bin/llvm-config ]; then
            mkdir ${BUILD64}
            cd ${BUILD64}
            cmake -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_TARGETS_TO_BUILD=${LLVM_TARGETS} -DLLVM_ENABLE_ASSERTIONS=ON -DCMAKE_BUILD_TYPE=Release ..
            make -j16
            cd ..
        fi
        cd ../../

        if [[ "$LLVM" == trunk ]]; then
            # Update this llvm and rebuild if it's trunk
            cd llvm/${LLVM}
            svn up &&
            cd tools/clang &&
            svn up &&
            cd ../../ &&
            make -j16 -C ${BUILD32} &&
            make -j16 -C ${BUILD64}
            cd ../../
        elif [[ "$LLVM" == pnacl ]]; then
            # Update this llvm and rebuild if it's pnacl
            cd llvm/${LLVM}
            git fetch &&
            # This version works with pepper_41
            git checkout b82e6c61c08dcd77a97cf6cb5020898bb49f97a8
            cd tools/clang &&
            git fetch &&
            git checkout a46d3ebfd8f295a32e2dab6241d207e04302b483
            cd ../../ &&
            make -j16 -C ${BUILD32} &&
            make -j16 -C ${BUILD64}
            cd ../../
        fi
    done

    for LLVM in ${LLVMS}; do
        if [[ "$LLVM" == pnacl ]]; then
            TARGETS="x86-32-nacl x86-32-sse41-nacl x86-64-nacl x86-64-sse41-nacl"
        elif [[ "$LLVM" == trunk ]]; then
            TARGETS="x86-32 x86-32-sse41 x86-64 x86-64-sse41 x86-64-avx host-cuda host-opencl"
        else
            TARGETS="x86-32 x86-32-sse41 x86-64 x86-64-sse41 x86-64-avx"
        fi

        for TARGET in $TARGETS; do
            echo Testing $LLVM $TARGET ...
            test/scripts/test_target.sh $LLVM $TARGET $COMPILER &> testing/reports/${HEAD}/testlog_${TARGET}_${LLVM}_${COMPILER}.txt
        done
    done
done

# Generate a summary of the results to go alongside the more detailed logs
ls testing/reports/${HEAD}/testlog_*.txt | sed "s/.*testlog_//" | sed "s/.txt//" > tmp/targets.txt
for f in testing/reports/${HEAD}/testlog_*.txt; do tail -n1 $f; done > tmp/results.txt
paste tmp/targets.txt tmp/results.txt > testing/reports/${HEAD}/summary.txt
rm tmp/targets.txt tmp/results.txt
