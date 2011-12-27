OCAMLBUILD=ocamlbuild
BUILD_CMD=${OCAMLBUILD} ${OCAMLBUILDFLAGS} 

# Mac Homebrew OCaml bindings install headers under /usr/local/lib/ocaml
CPPFLAGS=-I/usr/local/lib/ocaml
CCFLAGS=-std=c99
CFLAGS=${CPPFLAGS} ${CCFLAGS}

TARGETS=test_lower.ml test_stdlib.ml test_hello_ptx.ml
DEBUG_BYTE=$(patsubst %.ml,%.d.byte,${TARGETS})
DEBUG_NATIVE=$(patsubst %.ml,%.d.native,${TARGETS})
BYTE=$(patsubst %.ml,%.byte,${TARGETS})
NATIVE=$(patsubst %.ml,%.native,${TARGETS})

all: lib.byte lib.native

lib.byte: fimage.cma
lib.native: fimage.cmxa

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
