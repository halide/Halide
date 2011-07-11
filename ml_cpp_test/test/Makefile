LIB_DIR=$(shell pwd)..

all: test_img.native test_img.byte

img.cmx

test_img.native: test_img.ml img.cmxa
	ocamlbuild -cflags -I,$LIB_DIR -lflags -I,$LIB_DIR -lib img test_img.native