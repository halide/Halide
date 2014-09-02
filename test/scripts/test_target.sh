LLVM=$1

export HL_TARGET=$2
export HL_JIT_TARGET=$2

HOST=`uname`

if [[ "$HL_TARGET" == x86-3* ]]; then
    BITS=32
    if [ "$HOST" == Linux ]; then
        export LD="ld -melf_i386"
    else
        # OS X auto-detects the output format correctly
        export LD="ld"
    fi
    export CC="${CC} -m32"
    export CXX="${CXX} -m32"
    export GXX="${GXX} -m32"
    export CLANG=llvm/${LLVM}/build-32/bin/clang
    export LLVM_CONFIG=llvm/${LLVM}/build-32/bin/llvm-config
    export LIBPNG_LIBS="-Ltesting/deps -L../../testing/deps -lpng32 -lz32"
    export LIBPNG_CXX_FLAGS="-Itesting/deps -I../../testing/deps"
else
    BITS=64
    # ptx and opencl fall into this category
    export LD="ld"
    export CC="${CC} -m64"
    export CXX="${CXX} -m64"
    export GXX="${GXX} -m64"
    export CLANG=llvm/${LLVM}/build-64/bin/clang
    export LLVM_CONFIG=llvm/${LLVM}/build-64/bin/llvm-config
    export LIBPNG_LIBS="-Ltesting/deps -L../../testing/deps -lpng64 -lz64"
    export LIBPNG_CXX_FLAGS="-Itesting/deps -I../../testing/deps"
fi

echo Testing target $HL_TARGET with llvm $LLVM
echo Using LD = $LD
echo Using CC = $CC
echo Using CXX = $CXX
make clean &&
make -j8 build_tests || exit 1
make distrib || exit 1
DATE=`date +%Y_%m_%d`
COMMIT=`git rev-parse HEAD`
mv distrib/halide.tgz distrib/halide_${HOST}_${BITS}_${LLVM}_${COMMIT}_${DATE}.tgz
chmod a+r distrib/*

if [ "$HL_TARGET" == ptx -a "$HOST" == Darwin ]; then
    echo "Halide builds but tests not run"
elif [[ "$HL_TARGET" == *nacl ]]; then
    # The tests don't work for nacl yet. It's still worth testing that everything builds.

    # Also check that the nacl apps compile.
    make -C apps/HelloNaCl clean &&
    make -C apps/nacl_demos clean &&
    make -C apps/HelloNaCl &&
    make -C apps/nacl_demos &&
    echo "Halide builds but tests not run."
else
    make test_correctness -j8 &&
    make test_errors -j8 &&
    make test_static -j8 &&
    make test_tutorials -j8 &&
    make test_performance &&
    make test_apps &&
    echo "All tests pass"
fi

