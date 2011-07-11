all: img.cma img.cmxa

cimg.o:
	ocamlc -c -cc "gcc cimg.c -fPIC -Wall -std=c99" cimg.c

cimg.a: cimg.o
	cp /usr/lib/libpng.a libpng.a

dllcimg.so: libcimg.a

libcimg.a: cimg.o
	ocamlmklib $< -o cimg -lpng -lz -L/usr/lib/ocaml -lbigarray 

img.mli: img.ml
	ocamlc -i $< > $@

img.cmi: img.mli
	ocamlc -c $<

img.cmo: img.ml img.cmi
	ocamlc -c $<

img.cma: img.cmo dllcimg.so
	ocamlc -a -o $@ $< -dllib dllcimg.so

img.cmx: img.ml img.cmi
	ocamlopt -c $<

img.cmxa: img.cmx libcimg.a
	ocamlopt -a -o $@ $< -cclib -lcimg

clean:
	rm -f *.o *.a img.cm* img.mli libcimg.a dllcimg.so