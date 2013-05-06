OCAMLBUILD=ocamlbuild -use-ocamlfind
BUILD_CMD=${OCAMLBUILD} ${OCAMLBUILDFLAGS} 

LLVM_PATH=../llvm/Release+Asserts/bin
LLC=${LLVM_PATH}/llc
CLANG=${LLVM_PATH}/clang
CLANGPP=${LLVM_PATH}/clang++

CUDA_PATH=/usr/local/cuda
CUDA_LDFLAGS=-L${CUDA_PATH}/lib -lcuda

TARGETS=test_lower.ml test_stdlib.ml test_hello_ptx.ml
DEBUG_BYTE=$(patsubst %.ml,%.d.byte,${TARGETS})
DEBUG_NATIVE=$(patsubst %.ml,%.d.native,${TARGETS})
BYTE=$(patsubst %.ml,%.byte,${TARGETS})
NATIVE=$(patsubst %.ml,%.native,${TARGETS})

all: lib.byte lib.native libllsupport_impl.a

lib.llsupport: libllsupport_impl.a
lib.byte: halide.cma
lib.native: halide.cmxa

lib%.a:
	${BUILD_CMD} $@

%.cma:
	${BUILD_CMD} $@

%.cmxa:
	${BUILD_CMD} $@

%.byte: %.ml
	${BUILD_CMD} $@

%.native: %.ml
	${BUILD_CMD} $@

%.d.byte: %.ml
	${BUILD_CMD} $@

%.d.native: %.ml
	${BUILD_CMD} $@

byte: ${BYTE}

native: ${NATIVE}

debug: ${DEBUG_BYTE}

clean:
	${OCAMLBUILD} -clean

%.s: %.bc
	${LLC} $^

test_%.bc: test_%.byte
	${OCAMLBUILD} $^ --

test_hello_ptx: test_hello_ptx.cpp test_hello_ptx.s
	${CLANGPP} $^ ${CUDA_LDFLAGS} -o $@

run_test_hello_ptx: test_hello_ptx
	./test_hello_ptx
