FLAGS=-cflags -I,/Users/jrk/proj/fimage/FImage/llvm/build-Debug+Asserts/lib/ocaml/ -lflags -I,/Users/jrk/proj/fimage/FImage/llvm/build-Debug+Asserts/lib/ocaml/
OCAMLBUILD=ocamlbuild
BUILD_CMD=${OCAMLBUILD} ${FLAGS}

TARGETS=test.ml
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
