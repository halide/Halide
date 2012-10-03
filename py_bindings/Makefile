all: cHalide.i #halide.pyx #environ_fix
	swig -c++ -python -I../cpp_bindings cHalide.i
	CXX=g++ CC=g++ python setup.py build_ext --inplace
#	python setup.py build_ext --inplace

#environ_fix: environ_fix.cpp
#	g++ environ_fix.cpp -c
#	ar rs environ_fix.a environ_fix.o
	
clean:
	rm -rf build
	rm halide.so
	rm _cHalide.so
