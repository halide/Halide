LIB_DIR=$(shell pwd)/..

all: test_img.native test_img.byte

test_img.native: test_img.ml 
	ocamlopt -I $(LIB_DIR) -cclib "-L$(LIB_DIR) -lcimg -lpng -L -lbigarray" bigarray.cmxa test_img.ml -o test_img.native 

test_img.byte: test_img.ml
	ocamlc -I $(LIB_DIR) -dllpath $(LIB_DIR) -dllib dllcimg bigarray.cma test_img.ml -o test_img.byte

clean:
	rm -f test_img.byte test_img.native *.o *.cmi *.cmx