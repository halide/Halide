test: testml.o test.c
	gcc test.c testml.o -o test -L /usr/lib/ocaml -lasmrun -lm -ldl -Wall

testml.o: test.ml
	ocamlopt -output-obj test.ml -o testml.o

clean:
	rm -f *.o *.cmi *.cmx test