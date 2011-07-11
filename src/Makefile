# ocamlbuild includes require absolute paths, but do evaluate `..`
LLVM_ROOT=$(CURDIR)/../llvm/Debug+Asserts
LLVM_DIR=${LLVM_ROOT}/lib/ocaml/
CAMLIMAGES_DIR=$(CURDIR)/../camlimages/src/

LLVM_FLAGS=-cflags -I,${LLVM_DIR} -lflags -I,${LLVM_DIR}
CAMLIMAGES_FLAGS=-cflags -I,${CAMLIMAGES_DIR} -lflags -I,${CAMLIMAGES_DIR}

FLAGS=${LLVM_FLAGS} ${CAMLIMAGES_FLAGS} -cflags -annot

OCAMLBUILD=ocamlbuild
BUILD_CMD=${OCAMLBUILD} ${FLAGS}

TARGETS=cg_test.ml convolution.ml brightness.ml
DEBUG_BYTE=$(patsubst %.ml,%.d.byte,${TARGETS})
DEBUG_NATIVE=$(patsubst %.ml,%.d.native,${TARGETS})
BYTE=$(patsubst %.ml,%.byte,${TARGETS})
NATIVE=$(patsubst %.ml,%.native,${TARGETS})

%.byte: %.ml dummy
	${BUILD_CMD} $@

%.native: %.ml dummy
	${BUILD_CMD} $@

%.d.byte: %.ml dummy
	${BUILD_CMD} $@

%.d.native: %.ml dummy
	${BUILD_CMD} $@

byte: ${BYTE}

native: ${NATIVE}

debug: ${DEBUG_BYTE}

dummy:

.PHONY: dummy

clean:
	${OCAMLBUILD} -clean
