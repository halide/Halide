
for arch in ptx arm x86; do 

    C_STUB=architecture.${arch}.stdlib.c
    LL_STUB=architecture.${arch}.stdlib.ll
    RESULT=architecture.${arch}.target.c

    clang -emit-llvm -S $C_STUB -o -           \
        | grep -v "^target triple"             \
        | grep -v "^target datalayout"         \
        | grep -v "^; ModuleID"                \
        | cat - $LL_STUB                       \
        | llvm-as - -o -                       \
        | python bitcode2cpp.py ${arch}_target \
        > $RESULT

done