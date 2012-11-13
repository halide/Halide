export LLVM_PATH=`pwd`/llvm
export LLVM_PREFIX=${LLVM_PATH}/Release+Asserts
export PATH=${LLVM_PREFIX}/bin:${PATH}

export OCAMLPATH=${LLVM_PREFIX}/lib/ocaml:$OCAMLPATH
export CAML_LD_LIBRARY_PATH=${LLVM_PREFIX}/lib/ocaml:$CAML_LD_LIBRARY_PATH
# open LLVM ocamldoc
# alias lldoc="open ${LLVM_PATH}/docs/ocamldoc/html/index.html"
