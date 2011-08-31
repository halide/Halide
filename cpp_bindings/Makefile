OCAMLLIB=/usr/local/lib/ocaml
SRCDIR=../src
CXX=gcc-4.6 -std=c++0x -I$(OCAMLLIB) -I$(SRCDIR) -Wall -g
OCAMLC=ocamlopt -cc gcc-4.6 -I $(SRCDIR) 

# Stuff in this section doesn't need ocaml to compile. It is assumed
# that MLFImage.a and MLVal.h and MLFImage.h have been distributed.

all: test


LIBS=-L$(OCAMLLIB) -lbigarray -lllvm -lllvm_executionengine -lllvm_bitwriter -lllvm_analysis -lllvm_target $(shell llvm-config --libs all) -lm -ldl -lstdc++

test: MLFImage.a test.cpp 
	$(CXX) test.cpp $(OCAMLLIB)/libbigarray.a MLFImage.a -o test $(LIBS)

# Stuff below here is aware of ocaml, and requires it to compile

MLFImage.a: _MLFImage.o FImage.o MLVal.o
	cp $(OCAMLLIB)/libasmrun.a MLFImage.a
	ar q MLFImage.a _MLFImage.o MLVal.o FImage.o

%.o: %.cpp
	$(CXX) -c $< -o $@

OCAML_LIBS=bigarray.cmxa llvm.cmxa llvm_executionengine.cmxa
OCAML_MODULES=util.cmx ir.cmx ir_printer.cmx analysis.cmx cg_llvm.cmx MLFImage.cmx

_MLFImage.o: $(OCAML_MODULES)
	$(OCAMLC) $(OCAML_LIBS) $(OCAML_MODULES) -output-obj -o _MLFImage.o

%.cmx: $(SRCDIR)/%.ml
	$(OCAMLC) -c $< -o $@

%.cmx: %.ml
	$(OCAMLC) -c $< -o $@


clean:
	rm -f *.o *.cmi *.cmx *.cmo *.a test
