HL_MODULES = daubechies_x.bc inverse_daubechies_x.bc haar_x.bc inverse_haar_x.bc

all: filter

wavelet: ../../cpp_bindings/ wavelet.cpp
	$(MAKE) -C ../../cpp_bindings Halide.a
	g++-4.6 wavelet.cpp -I ../../cpp_bindings/ ../../cpp_bindings/Halide.a  -std=c++0x -o wavelet  -lpthread -ldl

$(HL_MODULES): wavelet 
	./wavelet

%.o: %.bc
	cat $< | opt -O3 | llc -O3 -filetype=obj -o=$@

filter.o: filter.cpp $(HL_MODULES:%.bc=%.o)
	g++-4.6 -std=c++0x -O3 -c filter.cpp

filter: filter.o $(HL_MODULES:%.bc=%.o)
	g++-4.6 filter.o $(HL_MODULES:%.bc=%.o) -lpng -o filter

clean:
	rm -f wavelet filter filter.o $(HL_MODULES) $(HL_MODULES:%.bc=%.o) $(HL_MODULES:%.bc=%.h) *.lowered