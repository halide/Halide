OCAMLLIB=/usr/local/lib/ocaml
CXX=gcc-4.6

# Stuff in this section doesn't need ocaml to compile. It is assumed
# that testml.a and mlval.h have been distributed.

test: testml.a test.cpp 
	$(CXX) -std=c++0x test.cpp testml.a -o test -lm -ldl -Wall -lstdc++

# Stuff below here is aware of ocaml, and requires it to compile

testml.a: testml.o mlval.o
	cp $(OCAMLLIB)/libasmrun.a testml.a
	ar q testml.a mlval.o testml.o

mlval.o: mlval.cpp
	$(CXX) -std=c++0x -c mlval.cpp -o mlval.o -I${OCAMLLIB} -Wall

testml.o: test.ml
	ocamlopt -output-obj test.ml -o testml.o

clean:
	rm -f *.o *.cmi *.cmx testml.a test
