CURR_DIR = $(shell pwd)
HALIDE_TOP = ${CURR_DIR}/../../../..
CC=/pkg/qct/software/gnu64/gcc/4.8.2/bin/g++
CXXFLAGS="--std=c++11"
HEXAGON_CLANG=${HEX_TOOLS}/bin/hexagon-clang++
CLANG_CPP=/pkg/qct/software/llvm/build_tools/llvm34_140306/bin/clang++
LDFLAGS=-L/pkg/qct/software/llvm/build_tools/libc++_140306/lib -L/pkg/qct/software/llvm/build_tools/libedit_tw/lib -L ${HALIDE_TOP}/bin -Wl,--start-group -lHalide -lpthread -ldl  -lc++ -Wl,--end-group
INCLUDES=-I${HALIDE_TOP}/include -I${HALIDE_TOP}/test/hexagon/include
TEST_TYPE?=realimage
gaussian3x3.o gaussian3x3.h: gaussian3x3.cpp
	${CLANG_CPP} -I/pkg/qct/software/llvm/build_tools/libc++_140306/include/c++/v1 -stdlib=libc++ gaussian3x3.cpp -std=c++11 -g -o gaussian3x3.generate.out -DRUN -O0 ${INCLUDES} ${LDFLAGS}
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./gaussian3x3.generate.out


gaussian3x3_run.o: gaussian3x3.o gaussian3x3.h gaussian3x3_run.cpp
	${HEX_TOOLS}/bin/hexagon-clang++ -c gaussian3x3_run.cpp -mhvx -mv60 -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include ${CXXFLAGS}

gaussian3x3.out: gaussian3x3_run.o gaussian3x3.o gaussian3x3.h
	${HEX_TOOLS}/bin/hexagon-link -march=hexagon -mcpu=hexagonv60 -o gaussian3x3.out ${HEX_TOOLS}/target/hexagon/lib/v60/crt0_standalone.o ${HEX_TOOLS}/target/hexagon/lib/v60/crt0.o ${HEX_TOOLS}/target/hexagon/lib/v60/init.o -L${HEX_TOOLS}/target/hexagon/lib -L${HEX_TOOLS}/target/hexagon/lib/v60 gaussian3x3_run.o gaussian3x3.o --start-group -lstandalone -lc -lgcc -lhexagon --end-group ${HEX_TOOLS}/target/hexagon/lib/v60/fini.o

all: gaussian3x3.out

clean:
	rm -rf *.o gaussian3x3.h *~ *.out *.bc *.ll *.s *.txt pmu* *.html *.iss.0 pa_dump.core.0 stats_dump.v60.iss.0 out.bin result; \
        $(MAKE) -C ref clean

.PHONY: assembly

assembly: gaussian3x3.cpp
	${CLANG_CPP} -I/pkg/qct/software/llvm/build_tools/libc++_140306/include/c++/v1 -stdlib=libc++ gaussian3x3.cpp -std=c++11 -g -o gaussian3x3.generate.out -DASSEMBLY -O0 ${INCLUDES} ${LDFLAGS}
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./gaussian3x3.generate.out

.PHONY: bitcode

bitcode: gaussian3x3.cpp
	${CLANG_CPP} -I/pkg/qct/software/llvm/build_tools/libc++_140306/include/c++/v1 -stdlib=libc++ gaussian3x3.cpp -std=c++11 -g -o gaussian3x3.generate.out -DBITCODE -O0 ${INCLUDES} ${LDFLAGS}
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
	if ! cmp out.bin ${CURR_DIR}/ref/golden_out.bin; then \
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
	${CLANG_CPP} -I/pkg/qct/software/llvm/build_tools/libc++_140306/include/c++/v1 -stdlib=libc++ gaussian3x3.cpp -std=c++11 -g -o gaussian3x3.generate.out -DSTMT -O0 ${INCLUDES} ${LDFLAGS}
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./gaussian3x3.generate.out

.PHONY: halide_synthetic

gaussian3x3_run.halide_synthetic.o: gaussian3x3.o gaussian3x3.h gaussian3x3_run.cpp
	${HEXAGON_CLANG} -c gaussian3x3_run.cpp -mhvx -mv60 -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include -DSYNTHETIC -o gaussian3x3_run.halide_synthetic.o


gaussian3x3.halide_synthetic.out: gaussian3x3_run.halide_synthetic.o gaussian3x3.o gaussian3x3.h
	${HEX_TOOLS}/bin/hexagon-link -march=hexagon -mcpu=hexagonv60 -o gaussian3x3.halide_synthetic.out ${HEX_TOOLS}/target/hexagon/lib/v60/crt0_standalone.o ${HEX_TOOLS}/target/hexagon/lib/v60/crt0.o ${HEX_TOOLS}/target/hexagon/lib/v60/init.o -L${HEX_TOOLS}/target/hexagon/lib -L${HEX_TOOLS}/target/hexagon/lib/v60 gaussian3x3_run.halide_synthetic.o gaussian3x3.o --start-group -lstandalone -lc -lgcc -lhexagon --end-group ${HEX_TOOLS}/target/hexagon/lib/v60/fini.o


halide_synthetic: gaussian3x3.halide_synthetic.out
	${HEX_TOOLS}/bin/hexagon-sim gaussian3x3.halide_synthetic.out --timing



.PHONY: ref ref_asm ref_int

ref_asm:
	$(MAKE) -C ref -f Makefile.${TEST_TYPE} clean all BUILD=ASM HALIDE_TOP=${CURR_DIR}/../../../..


ref_int:
	$(MAKE) -C ref -f Makefile.${TEST_TYPE} clean all BUILD=INT HALIDE_TOP=${CURR_DIR}/../../../..

ref: ref_asm ref_int