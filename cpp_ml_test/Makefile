OCAMLLIB=$(shell ocamlfind printconf stdlib)

# Stuff in this section doesn't need ocaml to compile. It is assumed
# that testml.a and mlval.h have been distributed.

test: testml.a test.cpp 
	g++ -std=c++0x test.cpp testml.a -o test -lm -ldl -Wall

# Stuff below here is aware of ocaml, and requires it to compile

testml.a: testml.o mlval.o
	cp $(OCAMLLIB)/libasmrun.a testml.a
	ar q testml.a mlval.o testml.o

mlval.o: mlval.cpp
	g++ -std=c++0x -c mlval.cpp -o mlval.o -I${OCAMLLIB} -Wall

testml.o: test.ml
	ocamlopt -output-obj test.ml -o testml.o

clean:
	rm -f *.o *.cmi *.cmx testml.a test
