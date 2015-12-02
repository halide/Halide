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
GDB=
CFLAGS:=${MHVX} -mv60 -DLOG2VLEN=${LOG2VLEN} ${EXTRA_FLAGS}
CXXFLAGS += ${EXTRA_FLAGS}
HALIDE_FUNC_CFLAGS = -DLOG2VLEN=${LOG2VLEN} ${EXTRA_FLAGS}
histogram.o histogram.h: histogram.cpp
	${X86_CLANG}  histogram.cpp -g -o histogram.generate.out -O0 -DRUN --std=c++11 ${INCLUDES} ${LDFLAGS} ${HALIDE_FUNC_CFLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ${GDB} ./histogram.generate.out


histogram_run.o: histogram.o histogram.h histogram_run.cpp
	${HEXAGON_CLANG} -c histogram_run.cpp ${CFLAGS} -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include

histogram.out: histogram_run.o histogram.o histogram.h
	${HEX_TOOLS}/bin/hexagon-link -march=hexagon -mcpu=hexagonv60 -o histogram.out ${HEX_TOOLS}/target/hexagon/lib/v60/crt0_standalone.o ${HEX_TOOLS}/target/hexagon/lib/v60/crt0.o ${HEX_TOOLS}/target/hexagon/lib/v60/init.o -L${HEX_TOOLS}/target/hexagon/lib -L${HEX_TOOLS}/target/hexagon/lib/v60 histogram_run.o histogram.o --start-group -lstandalone -lc -lgcc -lhexagon --end-group ${HEX_TOOLS}/target/hexagon/lib/v60/fini.o

all: histogram.out

clean:
	rm -rf *.o histogram.h *~ *.out *.bc *.ll *.s *.txt pmu* *.html *.iss.0 out.bin result run.std* pa_dump.core.0 stats_dump.v60.iss.0

.PHONY: assembly

assembly: histogram.cpp
	${X86_CLANG}  histogram.cpp -g  -o histogram.generate.out -O0 -DASSEMBLY --std=c++11 ${INCLUDES} ${LDFLAGS} ${HALIDE_FUNC_CFLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./histogram.generate.out

.PHONY: bitcode

bitcode: histogram.cpp
	${X86_CLANG}  histogram.cpp -g  -lHalide -lpthread -ldl -o histogram.generate.out -O0 -DBITCODE --std=c++11 ${INCLUDES} ${LDFLAGS} ${HALIDE_FUNC_CFLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./histogram.generate.out

.PHONY: all run bmark-report

all: histogram.out run bmark-report
bmark-report:
	if [ -e ${CURR_DIR}/run.stdout ]; then \
		grep -q PASS ${CURR_DIR}/result; \
		if [ $$? -eq 0 ];  then \
			grep "cycles/pixel" ${CURR_DIR}/run.stdout | awk '{print $$6"("$$4"):",$$7 " cycles/pixel"}'; \
		else \
			echo "${TEST_NAME}: FAIL";\
			exit 1; \
		fi \
	else \
		echo "run.stdout not found; please run make run first"; \
		exit 1; \
	fi

run: histogram.out
	${HEX_TOOLS}/bin/hexagon-sim histogram.out ${TIMING} -- 1920 1080 ${HALIDE_TOP}/test/hexagon/apps/testvectors/football1920x1080.bin out.bin 1>${CURR_DIR}/run.stdout 2>${CURR_DIR}/run.stderr; \
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
	  exit 1; \
        fi





.PHONY: stmt

stmt: histogram.cpp
	${X86_CLANG}  histogram.cpp -g  -lHalide -lpthread -ldl -o histogram.generate.out -O0 -DSTMT --std=c++11 ${INCLUDES} ${LDFLAGS} ${HALIDE_FUNC_CFLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./histogram.generate.out

.PHONY: synthetic

histogram_run.synthetic.o: histogram.o histogram.h histogram_run.cpp
	${HEXAGON_CLANG} -c histogram_run.cpp ${CFLAGS} -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include -DSYNTHETIC -o histogram_run.synthetic.o ${CXXFLAGS}


histogram.synthetic.out: histogram_run.synthetic.o histogram.o histogram.h
	${HEX_TOOLS}/bin/hexagon-link -march=hexagon -mcpu=hexagonv60 -o histogram.synthetic.out ${HEX_TOOLS}/target/hexagon/lib/v60/crt0_standalone.o ${HEX_TOOLS}/target/hexagon/lib/v60/crt0.o ${HEX_TOOLS}/target/hexagon/lib/v60/init.o -L${HEX_TOOLS}/target/hexagon/lib -L${HEX_TOOLS}/target/hexagon/lib/v60 histogram_run.synthetic.o histogram.o --start-group -lstandalone -lc -lgcc -lhexagon --end-group ${HEX_TOOLS}/target/hexagon/lib/v60/fini.o


synthetic: histogram.synthetic.out
	${HEX_TOOLS}/bin/hexagon-sim histogram.synthetic.out ${TIMING}
