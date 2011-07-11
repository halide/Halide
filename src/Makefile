# ocamlbuild includes require absolute paths, but do evaluate `..`
LLVM_ROOT=$(CURDIR)/../llvm/Debug+Asserts
LLVM_DIR=${LLVM_ROOT}/lib/ocaml/

LLVM_FLAGS=-cflags -I,${LLVM_DIR} -lflags -I,${LLVM_DIR}
IMG_FLAGS=-lflags cimg.o,-cclib,-lpng

OCAMLBUILDFLAGS=${LLVM_FLAGS} ${IMG_FLAGS} -cflags -annot

OCAMLBUILD=ocamlbuild
BUILD_CMD=${OCAMLBUILD} ${OCAMLBUILDFLAGS} 

# Mac Homebrew OCaml bindings install headers under /usr/local/lib/ocaml
CPPFLAGS=-I/usr/local/lib/ocaml
CCFLAGS=-std=c99
CFLAGS=${CPPFLAGS} ${CCFLAGS}

TARGETS=cg_test.ml convolution.ml brightness.ml
DEBUG_BYTE=$(patsubst %.ml,%.d.byte,${TARGETS})
DEBUG_NATIVE=$(patsubst %.ml,%.d.native,${TARGETS})
BYTE=$(patsubst %.ml,%.byte,${TARGETS})
NATIVE=$(patsubst %.ml,%.native,${TARGETS})

all: $(BYTE)

%.byte: %.ml cimg
	${BUILD_CMD} $@

%.native: %.ml cimg
	${BUILD_CMD} $@

%.d.byte: %.ml cimg
	${BUILD_CMD} $@

%.d.native: %.ml cimg
	${BUILD_CMD} $@

_build/cimg.o : cimg.c
	mkdir -p _build
	$(CC) ${CFLAGS} -c cimg.c -o _build/cimg.o -Wall

byte: ${BYTE}

native: ${NATIVE}

debug: ${DEBUG_BYTE}

cimg: _build/cimg.o

.PHONY: cimg

clean:
	${OCAMLBUILD} -clean
