LLVM_ROOT=`pwd`/../llvm
LLVM_PREFIX=${LLVM_ROOT}/Release+Asserts
LLVM_PATH=${LLVM_PREFIX}/bin
CLANG=${LLVM_PATH}/clang
LLVM_AS=${LLVM_PATH}/llvm-as

for arch in ptx ptx_dev x86 x86_avx arm arm_android; do 

    C_STUB=architecture.${arch}.stdlib.cpp
    LL_STUB=architecture.${arch}.stdlib.ll
    RESULT=architecture.${arch}.initmod.c
    CCFLAGS=""

    if [[ $arch == "ptx" ]]; then
      LL_STUB="$LL_STUB architecture.x86.stdlib.ll"
    fi

    if [[ $arch == "arm_android" ]]; then
        CCFLAGS="-m32"
    fi

    if [[ $arch = "x86" || $arch = "ptx" ]]; then
	CCFLAGS="-march=corei7"
    fi

    if [[ $arch = "x86_avx" ]]; then
	CCFLAGS="-march=corei7-avx"
	LL_STUB="architecture.x86.stdlib.ll architecture.x86_avx.stdlib.ll"
    fi

    $CLANG -emit-llvm -O3 $CCFLAGS -S $C_STUB -o -          \
        | grep -v "^target triple"             \
        | grep -v "^target datalayout"         \
        | grep -v "^; ModuleID"                \
        | cat - $LL_STUB                       \
        | $LLVM_AS - -o stub_${arch}.bc

    cat stub_${arch}.bc | python bitcode2cpp.py ${arch} > $RESULT

done
