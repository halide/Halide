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
X86_CLANG=/pkg/qct/software/llvm/build_tools/llvm34_140306/bin/clang++
LDFLAGS=-L/pkg/qct/software/llvm/build_tools/libc++_140306/lib -L/pkg/qct/software/llvm/build_tools/libedit_tw/lib -L ${HALIDE_TOP}/bin -Wl,--start-group -lHalide -lpthread -ldl  -lc++ -Wl,--end-group
INCLUDES=-I${HALIDE_TOP}/include -I${HALIDE_TOP}/test/hexagon/include -I/pkg/qct/software/llvm/build_tools/libc++_140306/include/c++/v1
TEST_NAME= $(shell basename ${CURR_DIR})
B128_MODE=
GDB=
CFLAGS:=${MHVX} -mv60 -DLOG2VLEN=${LOG2VLEN} # -DSYNTHETIC
sigma3x3.o sigma3x3.h: sigma3x3.cpp
	${X86_CLANG}  sigma3x3.cpp -g -o sigma3x3.generate.out -O0 -DRUN --std=c++11 ${INCLUDES} ${LDFLAGS} -DLOG2VLEN=${LOG2VLEN} ${EXTRA_FLAGS} ; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ${GDB} ./sigma3x3.generate.out  ${B128_MODE}


sigma3x3_run.o: sigma3x3.o sigma3x3.h sigma3x3_run.cpp
	${HEXAGON_CLANG} -c sigma3x3_run.cpp ${CFLAGS} -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include

sigma3x3.out: sigma3x3_run.o sigma3x3.o sigma3x3.h
	${HEX_TOOLS}/bin/hexagon-link -march=hexagon -mcpu=hexagonv60 -o sigma3x3.out ${HEX_TOOLS}/target/hexagon/lib/v60/crt0_standalone.o ${HEX_TOOLS}/target/hexagon/lib/v60/crt0.o ${HEX_TOOLS}/target/hexagon/lib/v60/init.o -L${HEX_TOOLS}/target/hexagon/lib -L${HEX_TOOLS}/target/hexagon/lib/v60 sigma3x3_run.o sigma3x3.o --start-group -lstandalone -lc -lgcc -lhexagon --end-group ${HEX_TOOLS}/target/hexagon/lib/v60/fini.o

all: sigma3x3.out

clean:
	rm -rf *.o sigma3x3.h *~ *.out *.bc *.ll *.s *.txt pmu* *.html *.iss.0 out.bin result run.std* pa_dump.core.0 stats_dump.v60.iss.0

.PHONY: assembly

assembly: sigma3x3.cpp
	${X86_CLANG}  sigma3x3.cpp -g  -o sigma3x3.generate.out -O0 -DASSEMBLY --std=c++11 ${INCLUDES} ${LDFLAGS} ${EXTRA_FLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./sigma3x3.generate.out ${B128_MODE}

.PHONY: bitcode

bitcode: sigma3x3.cpp
	${X86_CLANG}  sigma3x3.cpp -g  -lHalide -lpthread -ldl -o sigma3x3.generate.out -O0 -DBITCODE --std=c++11 ${INCLUDES} ${LDFLAGS} ${EXTRA_FLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./sigma3x3.generate.out ${B128_MODE}

.PHONY: all run bmark-report

all: sigma3x3.out run bmark-report
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

run: sigma3x3.out
	${HEX_TOOLS}/bin/hexagon-sim sigma3x3.out ${TIMING} -- 1920 1080 ${HALIDE_TOP}/test/hexagon/apps/testvectors/football1920x1080.bin out.bin 1>${CURR_DIR}/run.stdout 2>${CURR_DIR}/run.stderr; \
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

stmt: sigma3x3.cpp
	${X86_CLANG}  sigma3x3.cpp -g  -lHalide -lpthread -ldl -o sigma3x3.generate.out -O0 -DSTMT --std=c++11 ${INCLUDES} ${LDFLAGS} ${EXTRA_FLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./sigma3x3.generate.out

.PHONY: synthetic

sigma3x3_run.synthetic.o: sigma3x3.o sigma3x3.h sigma3x3_run.cpp
	${HEXAGON_CLANG} -c sigma3x3_run.cpp ${CFLAGS} -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include -DSYNTHETIC -o sigma3x3_run.synthetic.o ${CXXFLAGS}


sigma3x3.synthetic.out: sigma3x3_run.synthetic.o sigma3x3.o sigma3x3.h
	${HEX_TOOLS}/bin/hexagon-link -march=hexagon -mcpu=hexagonv60 -o sigma3x3.synthetic.out ${HEX_TOOLS}/target/hexagon/lib/v60/crt0_standalone.o ${HEX_TOOLS}/target/hexagon/lib/v60/crt0.o ${HEX_TOOLS}/target/hexagon/lib/v60/init.o -L${HEX_TOOLS}/target/hexagon/lib -L${HEX_TOOLS}/target/hexagon/lib/v60 sigma3x3_run.synthetic.o sigma3x3.o --start-group -lstandalone -lc -lgcc -lhexagon --end-group ${HEX_TOOLS}/target/hexagon/lib/v60/fini.o


synthetic: sigma3x3.synthetic.out
	${HEX_TOOLS}/bin/hexagon-sim sigma3x3.synthetic.out ${TIMING}
