# ocamlbuild includes require absolute paths, but do evaluate `..`
LLVM_ROOT=$(CURDIR)/../llvm/build-Debug+Asserts
LLVM_DIR=${LLVM_ROOT}/lib/ocaml/
CAMLIMAGES_DIR=$(CURDIR)/../camlimages/src/

LLVM_FLAGS=-cflags -I,${LLVM_DIR} -lflags -I,${LLVM_DIR}
CAMLIMAGES_FLAGS=-cflags -I,${CAMLIMAGES_DIR} -lflags -I,${CAMLIMAGES_DIR}

FLAGS=${LLVM_FLAGS} ${CAMLIMAGES_FLAGS}

OCAMLBUILD=ocamlbuild
BUILD_CMD=${OCAMLBUILD} ${FLAGS}

test.byte: test.ml
	${BUILD_CMD} $@

test.native: test.ml
	${BUILD_CMD} $@

clean:
	${OCAMLBUILD} -clean
