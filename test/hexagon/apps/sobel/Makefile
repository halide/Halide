
HALIDE_TOP = `pwd`/../../../..
CC=/pkg/qct/software/gnu64/gcc/4.8.2/bin/g++
CXXFLAGS="--std=c++11"


sobel.o sobel.h: sobel.cpp
	${CC} sobel.cpp -g -I ${HALIDE_TOP}/include -L ${HALIDE_TOP}/bin -lHalide -lpthread -ldl -o sobel.generate.out -O0 -DRUN -I ${HALIDE_TOP}/test/hexagon ${CXXFLAGS}
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:$LD_LIBRARY_PATH
	./sobel.generate.out


sobel_run.o: sobel.o sobel.h sobel_run.cpp
	${CLANG} -c sobel_run.cpp -mhvx -mv60 -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include

sobel.out: sobel_run.o sobel.o sobel.h
	${HEX_TOOLS}/bin/hexagon-link -march=hexagon -mcpu=hexagonv60 -o sobel.out ${HEX_TOOLS}/target/hexagon/lib/v60/crt0_standalone.o ${HEX_TOOLS}/target/hexagon/lib/v60/crt0.o ${HEX_TOOLS}/target/hexagon/lib/v60/init.o -L${HEX_TOOLS}/target/hexagon/lib -L${HEX_TOOLS}/target/hexagon/lib/v60 sobel_run.o sobel.o --start-group -lstandalone -lc -lgcc -lhexagon --end-group ${HEX_TOOLS}/target/hexagon/lib/v60/fini.o

all: sobel.out

clean:
	rm -rf *.o sobel.h *~ *.out *.bc *.ll *.s *.txt pmu* *.html

.PHONY: assembly

assembly: sobel.cpp
	${CC} sobel.cpp -g -I ${HALIDE_TOP}/include -L ${HALIDE_TOP}/bin -lHalide -lpthread -ldl -o sobel.generate.out -O0 -DASSEMBLY -I ${HALIDE_TOP}/test/hexagon ${CXXFLAGS}
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:$LD_LIBRARY_PATH
	./sobel.generate.out

.PHONY: bitcode

bitcode: sobel.cpp
	${CC} sobel.cpp -g -I ${HALIDE_TOP}/include -L ${HALIDE_TOP}/bin -lHalide -lpthread -ldl -o sobel.generate.out -O0 -DBITCODE -I ${HALIDE_TOP}/test/hexagon ${CXXFLAGS}
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:$LD_LIBRARY_PATH
	./sobel.generate.out

.PHONY: all

all: sobel.out run


run: sobel.out
	${HEX_TOOLS}/bin/hexagon-sim sobel.out --timing


.PHONY: stmt

stmt: sobel.cpp
	${CC} sobel.cpp -g -I ${HALIDE_TOP}/include -L ${HALIDE_TOP}/bin -lHalide -lpthread -ldl -o sobel.generate.out -O0 -DSTMT -I ${HALIDE_TOP}/test/hexagon ${CXXFLAGS}
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:$LD_LIBRARY_PATH
	./sobel.generate.out
