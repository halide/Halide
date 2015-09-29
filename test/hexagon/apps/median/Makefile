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
CXXFLAGS="--std=c++11"
HEXAGON_CLANG=${HEX_TOOLS}/bin/hexagon-clang++
X86_CLANG=clang++
LDFLAGS=-L/pkg/qct/software/llvm/build_tools/libc++_140306/lib -L/pkg/qct/software/llvm/build_tools/libedit_tw/lib -L ${HALIDE_TOP}/bin -Wl,--start-group -lHalide -lpthread -ldl  -lc++ -Wl,--end-group
INCLUDES=-I${HALIDE_TOP}/include -I${HALIDE_TOP}/test/hexagon/include -I/pkg/qct/software/llvm/build_tools/libc++_140306/include/c++/v1
TEST_NAME= $(shell basename ${CURR_DIR})
B128_MODE=
GDB=
CFLAGS:=${MHVX} -mv60 -DLOG2VLEN=${LOG2VLEN}
median.o median.h: median.cpp
	${X86_CLANG}  median.cpp -g -o median.generate.out -O0 -DRUN --std=c++11 ${INCLUDES} ${LDFLAGS} -DLOG2VLEN=${LOG2VLEN} ${EXTRA_FLAGS} ; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ${GDB} ./median.generate.out  ${B128_MODE}


median_run.o: median.o median.h median_run.cpp
	${HEXAGON_CLANG} -c median_run.cpp ${CFLAGS} -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include

median.out: median_run.o median.o median.h
	${HEX_TOOLS}/bin/hexagon-link -march=hexagon -mcpu=hexagonv60 -o median.out ${HEX_TOOLS}/target/hexagon/lib/v60/crt0_standalone.o ${HEX_TOOLS}/target/hexagon/lib/v60/crt0.o ${HEX_TOOLS}/target/hexagon/lib/v60/init.o -L${HEX_TOOLS}/target/hexagon/lib -L${HEX_TOOLS}/target/hexagon/lib/v60 median_run.o median.o --start-group -lstandalone -lc -lgcc -lhexagon --end-group ${HEX_TOOLS}/target/hexagon/lib/v60/fini.o

all: median.out

clean:
	rm -rf *.o median.h *~ *.out *.bc *.ll *.s *.txt pmu* *.html *.iss.0 out.bin result run.std* pa_dump.core.0 stats_dump.v60.iss.0

.PHONY: assembly

assembly: median.cpp
	${X86_CLANG}  median.cpp -g  -o median.generate.out -O0 -DASSEMBLY --std=c++11 ${INCLUDES} ${LDFLAGS} ${EXTRA_FLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./median.generate.out ${B128_MODE}

.PHONY: bitcode

bitcode: median.cpp
	${X86_CLANG}  median.cpp -g  -lHalide -lpthread -ldl -o median.generate.out -O0 -DBITCODE --std=c++11 ${INCLUDES} ${LDFLAGS} ${EXTRA_FLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./median.generate.out ${B128_MODE}

.PHONY: all run bmark-report

all: median.out run bmark-report
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

run: median.out
	${HEX_TOOLS}/bin/hexagon-sim median.out ${TIMING} -- 1920 1080 ${HALIDE_TOP}/test/hexagon/apps/testvectors/football1920x1080.bin out.bin 1>${CURR_DIR}/run.stdout 2>${CURR_DIR}/run.stderr; \
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

stmt: median.cpp
	${X86_CLANG}  median.cpp -g  -lHalide -lpthread -ldl -o median.generate.out -O0 -DSTMT --std=c++11 ${INCLUDES} ${LDFLAGS} ${EXTRA_FLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./median.generate.out

.PHONY: synthetic

median_run.synthetic.o: median.o median.h median_run.cpp
	${HEXAGON_CLANG} -c median_run.cpp ${CFLAGS} -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include -DSYNTHETIC -o median_run.synthetic.o ${CXXFLAGS}


median.synthetic.out: median_run.synthetic.o median.o median.h
	${HEX_TOOLS}/bin/hexagon-link -march=hexagon -mcpu=hexagonv60 -o median.synthetic.out ${HEX_TOOLS}/target/hexagon/lib/v60/crt0_standalone.o ${HEX_TOOLS}/target/hexagon/lib/v60/crt0.o ${HEX_TOOLS}/target/hexagon/lib/v60/init.o -L${HEX_TOOLS}/target/hexagon/lib -L${HEX_TOOLS}/target/hexagon/lib/v60 median_run.synthetic.o median.o --start-group -lstandalone -lc -lgcc -lhexagon --end-group ${HEX_TOOLS}/target/hexagon/lib/v60/fini.o


synthetic: median.synthetic.out
	${HEX_TOOLS}/bin/hexagon-sim median.synthetic.out ${TIMING}
