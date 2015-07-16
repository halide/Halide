HALIDE_TOP = `pwd`/../../../..
CURR_DIR = `pwd`
CC=/pkg/qct/software/gnu64/gcc/4.8.2/bin/g++
CXXFLAGS="--std=c++11"
CLANG=hexagon-clang++
CURR_DIR = `pwd`


gaussian3x3.o gaussian3x3.h: gaussian3x3.cpp
	${CC} gaussian3x3.cpp -g -I ${HALIDE_TOP}/include -L ${HALIDE_TOP}/bin -lHalide -lpthread -ldl -o gaussian3x3.generate.out -O0 -DRUN -I ${HALIDE_TOP}/test/hexagon/include -v ${CXXFLAGS}
	./gaussian3x3.generate.out


gaussian3x3_run.o: gaussian3x3.o gaussian3x3.h gaussian3x3_run.cpp
	${HEX_TOOLS}/bin/hexagon-clang++ -c gaussian3x3_run.cpp -mhvx -mv60 -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include ${CXXFLAGS}

gaussian3x3.out: gaussian3x3_run.o gaussian3x3.o gaussian3x3.h
	${HEX_TOOLS}/bin/hexagon-link -march=hexagon -mcpu=hexagonv60 -o gaussian3x3.out ${HEX_TOOLS}/target/hexagon/lib/v60/crt0_standalone.o ${HEX_TOOLS}/target/hexagon/lib/v60/crt0.o ${HEX_TOOLS}/target/hexagon/lib/v60/init.o -L${HEX_TOOLS}/target/hexagon/lib -L${HEX_TOOLS}/target/hexagon/lib/v60 gaussian3x3_run.o gaussian3x3.o --start-group -lstandalone -lc -lgcc -lhexagon --end-group ${HEX_TOOLS}/target/hexagon/lib/v60/fini.o

all: gaussian3x3.out

clean:
	rm -rf *.o gaussian3x3.h *~ *.out *.bc *.ll *.s *.txt pmu* *.html *.iss.0 out.bin result

.PHONY: assembly

assembly: gaussian3x3.cpp
	${CC} gaussian3x3.cpp -g -I ${HALIDE_TOP}/include -L ${HALIDE_TOP}/bin -lHalide -lpthread -ldl -o gaussian3x3.generate.out -O0 -DASSEMBLY -I ${HALIDE_TOP}/test/hexagon/include ${CXXFLAGS}
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./gaussian3x3.generate.out

.PHONY: bitcode

bitcode: gaussian3x3.cpp
	${CC} gaussian3x3.cpp -g -I ${HALIDE_TOP}/include -L ${HALIDE_TOP}/bin -lHalide -lpthread -ldl -o gaussian3x3.generate.out -O0 -DBITCODE -I ${HALIDE_TOP}/test/hexagon/include ${CXXFLAGS}
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./gaussian3x3.generate.out

.PHONY: all

all: gaussian3x3.out run


run: gaussian3x3.out
	${HEX_TOOLS}/bin/hexagon-sim gaussian3x3.out --timing -- 1920 1080 ${HALIDE_TOP}/test/hexagon/apps/testvectors/football1920x1080.bin out.bin; \
	RUN_STATUS=$$?; \
	FAIL=0; \
	if [ $$RUN_STATUS -ne 0 ]; then \
	  FAIL=1; \
	fi; \
	if ! cmp out.bin ${CURR_DIR}/golden_out.bin; then \
          FAIL=1; \
        fi; \
	if [ $$FAIL -eq 0 ]; then \
          echo PASS > ${CURR_DIR}/result; \
	  echo PASS; \
        else \
          echo FAIL > ${CURR_DIR}/result; \
	  echo FAIL;\
        fi





.PHONY: stmt

stmt: gaussian3x3.cpp
	${CC} gaussian3x3.cpp -g -I ${HALIDE_TOP}/include -L ${HALIDE_TOP}/bin -lHalide -lpthread -ldl -o gaussian3x3.generate.out -O0 -DSTMT -I ${HALIDE_TOP}/test/hexagon/include ${CXXFLAGS}
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./gaussian3x3.generate.out

.PHONY: synthetic

gaussian3x3_run.synthetic.o: gaussian3x3.o gaussian3x3.h gaussian3x3_run.cpp
	${CLANG} -c gaussian3x3_run.cpp -mhvx -mv60 -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include -DSYNTHETIC -o gaussian3x3_run.synthetic.o


gaussian3x3.synthetic.out: gaussian3x3_run.synthetic.o gaussian3x3.o gaussian3x3.h
	${HEX_TOOLS}/bin/hexagon-link -march=hexagon -mcpu=hexagonv60 -o gaussian3x3.synthetic.out ${HEX_TOOLS}/target/hexagon/lib/v60/crt0_standalone.o ${HEX_TOOLS}/target/hexagon/lib/v60/crt0.o ${HEX_TOOLS}/target/hexagon/lib/v60/init.o -L${HEX_TOOLS}/target/hexagon/lib -L${HEX_TOOLS}/target/hexagon/lib/v60 gaussian3x3_run.synthetic.o gaussian3x3.o --start-group -lstandalone -lc -lgcc -lhexagon --end-group ${HEX_TOOLS}/target/hexagon/lib/v60/fini.o


synthetic: gaussian3x3.synthetic.out
	${HEX_TOOLS}/bin/hexagon-sim gaussian3x3.synthetic.out --timing
