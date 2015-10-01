CURR_DIR = $(shell pwd)
HALIDE_TOP = ${CURR_DIR}/../../../..
# Vector Length
ifeq (,$(LOG2VLEN))
  LOG2VLEN:=7
endif

ifeq (7,$(LOG2VLEN))
  MHVX:=-mhvx-double
else
  MHVX:=-mhvx
endif

TIMING=--timing
CC=/pkg/qct/software/gnu64/gcc/4.8.2/bin/g++
CXXFLAGS=--std=c++11 -DLOG2VLEN=${LOG2VLEN}
HEXAGON_CLANG=${HEX_TOOLS}/bin/hexagon-clang++
X86_CLANG=clang++
LDFLAGS=-L/pkg/qct/software/llvm/build_tools/libc++_140306/lib -L/pkg/qct/software/llvm/build_tools/libedit_tw/lib -L ${HALIDE_TOP}/bin -Wl,--start-group -lHalide -lpthread -ldl  -lc++ -Wl,--end-group
INCLUDES=-I${HALIDE_TOP}/include -I${HALIDE_TOP}/test/hexagon/include -I/pkg/qct/software/llvm/build_tools/libc++_140306/include/c++/v1
TEST_NAME= $(shell basename ${CURR_DIR})
B128_MODE=
GDB=
CFLAGS:=${MHVX} -mv60 -DLOG2VLEN=${LOG2VLEN} ${EXTRA_FLAGS}
CXXFLAGS += ${EXTRA_FLAGS}
HALIDE_FUNC_CFLAGS = -DLOG2VLEN=${LOG2VLEN} ${EXTRA_FLAGS}
gaussian5x5.o gaussian5x5.h: gaussian5x5.cpp
	${X86_CLANG}  gaussian5x5.cpp -g -o gaussian5x5.generate.out -O0 -DRUN --std=c++11 ${INCLUDES} ${LDFLAGS} ${HALIDE_FUNC_CFLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ${GDB} ./gaussian5x5.generate.out  ${B128_MODE}


gaussian5x5_run.o: gaussian5x5.o gaussian5x5.h gaussian5x5_run.cpp
	${HEXAGON_CLANG} -c gaussian5x5_run.cpp ${CFLAGS} -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include

gaussian5x5.out: gaussian5x5_run.o gaussian5x5.o gaussian5x5.h
	${HEX_TOOLS}/bin/hexagon-link -march=hexagon -mcpu=hexagonv60 -o gaussian5x5.out ${HEX_TOOLS}/target/hexagon/lib/v60/crt0_standalone.o ${HEX_TOOLS}/target/hexagon/lib/v60/crt0.o ${HEX_TOOLS}/target/hexagon/lib/v60/init.o -L${HEX_TOOLS}/target/hexagon/lib -L${HEX_TOOLS}/target/hexagon/lib/v60 gaussian5x5_run.o gaussian5x5.o --start-group -lstandalone -lc -lgcc -lhexagon --end-group ${HEX_TOOLS}/target/hexagon/lib/v60/fini.o

all: gaussian5x5.out

clean:
	rm -rf *.o gaussian5x5.h *~ *.out *.bc *.ll *.s *.txt pmu* *.html *.iss.0 out.bin result run.std* pa_dump.core.0 stats_dump.v60.iss.0

.PHONY: assembly

assembly: gaussian5x5.cpp
	${X86_CLANG}  gaussian5x5.cpp -g  -o gaussian5x5.generate.out -O0 -DASSEMBLY --std=c++11 ${INCLUDES} ${LDFLAGS} ${HALIDE_FUNC_CFLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./gaussian5x5.generate.out ${B128_MODE}

.PHONY: bitcode

bitcode: gaussian5x5.cpp
	${X86_CLANG}  gaussian5x5.cpp -g  -lHalide -lpthread -ldl -o gaussian5x5.generate.out -O0 -DBITCODE --std=c++11 ${INCLUDES} ${LDFLAGS} ${HALIDE_FUNC_CFLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./gaussian5x5.generate.out ${B128_MODE}

.PHONY: all run bmark-report

all: gaussian5x5.out run bmark-report
bmark-report:
	if [ -e ${CURR_DIR}/run.stdout ]; then \
		grep -q PASS ${CURR_DIR}/result; \
		if [ $$? -eq 0 ];  then \
			grep "cycles/pixel" ${CURR_DIR}/run.stdout | awk '{print $$6"("$$4"):",$$7 " cycles/pixel"}'; \
		else \
			echo "${TEST_NAME}: FAIL";\
		fi \
	else \
		echo "run.stdout not found; please run make run first"; \
	fi

run: gaussian5x5.out
	${HEX_TOOLS}/bin/hexagon-sim gaussian5x5.out ${TIMING} -- 1920 1080 ${HALIDE_TOP}/test/hexagon/apps/testvectors/football1920x1080.bin out.bin 1>${CURR_DIR}/run.stdout 2>${CURR_DIR}/run.stderr; \
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

stmt: gaussian5x5.cpp
	${X86_CLANG}  gaussian5x5.cpp -g  -lHalide -lpthread -ldl -o gaussian5x5.generate.out -O0 -DSTMT --std=c++11 ${INCLUDES} ${LDFLAGS} ${HALIDE_FUNC_CFLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./gaussian5x5.generate.out

.PHONY: synthetic

gaussian5x5_run.synthetic.o: gaussian5x5.o gaussian5x5.h gaussian5x5_run.cpp
	${HEXAGON_CLANG} -c gaussian5x5_run.cpp ${CFLAGS} -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include -DSYNTHETIC -o gaussian5x5_run.synthetic.o ${CXXFLAGS}


gaussian5x5.synthetic.out: gaussian5x5_run.synthetic.o gaussian5x5.o gaussian5x5.h
	${HEX_TOOLS}/bin/hexagon-link -march=hexagon -mcpu=hexagonv60 -o gaussian5x5.synthetic.out ${HEX_TOOLS}/target/hexagon/lib/v60/crt0_standalone.o ${HEX_TOOLS}/target/hexagon/lib/v60/crt0.o ${HEX_TOOLS}/target/hexagon/lib/v60/init.o -L${HEX_TOOLS}/target/hexagon/lib -L${HEX_TOOLS}/target/hexagon/lib/v60 gaussian5x5_run.synthetic.o gaussian5x5.o --start-group -lstandalone -lc -lgcc -lhexagon --end-group ${HEX_TOOLS}/target/hexagon/lib/v60/fini.o


synthetic: gaussian5x5.synthetic.out
	${HEX_TOOLS}/bin/hexagon-sim gaussian5x5.synthetic.out ${TIMING}
