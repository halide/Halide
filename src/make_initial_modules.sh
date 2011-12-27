
for arch in ptx ptx_dev arm x86; do 

    C_STUB=architecture.${arch}.stdlib.cpp
    LL_STUB=architecture.${arch}.stdlib.ll
    RESULT=architecture.${arch}.initmod.c

    clang -emit-llvm -S $C_STUB -o -           \
        | grep -v "^target triple"             \
        | grep -v "^target datalayout"         \
        | grep -v "^; ModuleID"                \
        | cat - $LL_STUB                       \
        | llvm-as - -o -                       \
        | python bitcode2cpp.py ${arch}        \
        > $RESULT

done