export LLVM_PATH=`pwd`/llvm
export LLVM_PREFIX=${LLVM_PATH}/Release+Asserts
export PATH=${LLVM_PREFIX}/bin:${PATH}
export PATH=${PATH}:/usr/local/Cellar/gcc/4.6/bin

# open LLVM ocamldoc
alias lldoc="open ${LLVM_PATH}/docs/ocamldoc/html/index.html"

export IMAGESTACK_PREFIX=`pwd`/ImageStack
export PATH=${IMAGESTACK_PREFIX}/bin:${PATH}
