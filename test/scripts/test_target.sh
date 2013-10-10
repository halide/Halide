LLVM=$1

export HL_TARGET=$2

if [[ "$HL_TARGET" == x86-3* ]]; then
    BITS=32
    UNAME=`uname`
    if [[ `uname` == Linux ]]; then
        export LD="ld -melf_i386"
    else
        # OS X auto-detects the output format correctly
        export LD="ld"
    fi
    export CC="gcc -m32"
    export CXX="g++ -m32"
    export GXX="g++ -m32"
    export CLANG=llvm/${LLVM}/build-32/bin/clang
    export LLVM_CONFIG=llvm/${LLVM}/build-32/bin/llvm-config
    export LIBPNG_LIBS="-Ltesting/deps -L../../testing/deps -lpng32 -lz32"
    export LIBPNG_CXX_FLAGS="-Itesting/deps -I../../testing/deps"
else
    BITS=64
    # ptx falls into this category
    export LD="ld"
    export CC="gcc -m64"
    export CXX="g++ -m64"
    export GXX="g++ -m64"
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
HOST=`uname`
mv distrib/halide.tgz distrib/halide_${HOST}_${BITS}_${LLVM}_${DATE}.tgz
if [[ "$HL_TARGET" == *nacl ]]; then
    # The tests don't work for nacl yet. It's still worth testing that everything builds.
    echo "Halide builds but tests not run."
else
    make run_tests -j8 &&
    make test_apps &&
    echo "All tests pass"
fi

