export LLVM_PATH=`pwd`/llvm
export LLVM_PREFIX=${LLVM_PATH}/Debug+Asserts
export PATH=${LLVM_PREFIX}/bin:${PATH}

# open LLVM ocamldoc
alias lldoc="open ${LLVM_PATH}/docs/ocamldoc/html/index.html"

export IMAGESTACK_PREFIX=`pwd`/ImageStack
export PATH=${IMAGESTACK_PREFIX}/bin:${PATH}
