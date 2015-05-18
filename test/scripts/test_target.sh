LLVM=$1

export HL_TARGET=$2
export HL_JIT_TARGET=$2

COMPILER=$3

HOST=`uname`

if [[ "$HL_TARGET" == x86-3* ]]; then
    BITS=32
    if [ "$HOST" == Linux ]; then
        export LD="ld -melf_i386"
    else
        # OS X auto-detects the output format correctly
        export LD="ld"
    fi
    export CXX="${CXX} -m32"
    export CLANG=llvm/${LLVM}/build-32-${COMPILER}/bin/clang
    export LLVM_CONFIG=llvm/${LLVM}/build-32-${COMPILER}/bin/llvm-config
    export LIBPNG_LIBS="-Ltesting/deps -L../../testing/deps -lpng32 -lz32"
    export LIBPNG_CXX_FLAGS="-Itesting/deps -I../../testing/deps"
else
    BITS=64
    # cuda (ptx) and opencl fall into this category
    export LD="ld"
    export CXX="${CXX} -m64"
    export CLANG=llvm/${LLVM}/build-64-${COMPILER}/bin/clang
    export LLVM_CONFIG=llvm/${LLVM}/build-64-${COMPILER}/bin/llvm-config
    export LIBPNG_LIBS="-Ltesting/deps -L../../testing/deps -lpng64 -lz64"
    export LIBPNG_CXX_FLAGS="-Itesting/deps -I../../testing/deps"
fi

# Turn on C++11
export CXX11=true

echo Testing target $HL_TARGET with llvm $LLVM compiled with $COMPILER
echo Using LD = $LD
echo Using CXX = $CXX
make clean &&
make -j16 build_tests || exit 1
make distrib || exit 1
DATE=`date +%Y_%m_%d`
COMMIT=`git rev-parse HEAD`
mv distrib/halide.tgz distrib/halide_${HOST}_${BITS}_${LLVM}_${COMPILER}_${COMMIT}_${DATE}.tgz
chmod a+r distrib/*

if [ "$HL_TARGET" == "host-cuda" -a "$HOST" == Darwin ]; then
    echo "Halide builds but tests not run"
elif [ "$HL_TARGET" == "host-opencl" -a "$HOST" == Darwin ]; then
    echo "Halide builds but tests not run"
elif [[ "$HL_TARGET" == *nacl ]]; then
    # The tests don't work for nacl yet. It's still worth testing that everything builds.

    # Also check that the nacl apps compile.
    make -C apps/nacl_demos clean &&
    make -C apps/nacl_demos &&
    echo "Halide builds but tests not run."
elif [ "$QUICK_AND_DIRTY" ]; then
    make test_argmax
    echo "Everything built and test_argmax passed. Not running other tests because QUICK_AND_DIRTY is set."
else
    make test_correctness -j16 &&
    make test_errors -j16 &&
    make test_tutorials -j16 &&
    make test_performance &&
    make test_apps &&
    make test_generators &&
    echo "All tests pass"
fi
