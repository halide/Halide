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
ARG12=256 1

CXXFLAGS=--std=c++11 -DLOG2VLEN=${LOG2VLEN} ${EXTRA_FLAGS}
HEXAGON_CLANG=${HEX_TOOLS}/bin/hexagon-clang++
LDFLAGS=-L ${HALIDE_TOP}/bin -lHalide
INCLUDES=-I${HALIDE_TOP}/include -I${HALIDE_TOP}/test/hexagon/include
TEST_NAME= $(shell basename ${CURR_DIR})
B128_MODE=
VERSION_FLAG=-DRDOM
GDB=
CFLAGS:=${MHVX} -mv60 -DLOG2VLEN=${LOG2VLEN} ${EXTRA_FLAGS}
invsqrt.o invsqrt.h: invsqrt.cpp
	${CXX}  invsqrt.cpp -g -o invsqrt.generate.out -O0 -DRUN --std=c++11 ${INCLUDES} ${LDFLAGS} -DLOG2VLEN=${LOG2VLEN} ${EXTRA_FLAGS} ${VERSION_FLAG} ; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ${GDB} ./invsqrt.generate.out  ${B128_MODE}


invsqrt_run.o: invsqrt.o invsqrt.h invsqrt_run.cpp
	${HEXAGON_CLANG} -c invsqrt_run.cpp ${CFLAGS} -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include

invsqrt.out: invsqrt_run.o invsqrt.o invsqrt.h
	${HEXAGON_CLANG} -mv60 -o invsqrt.out invsqrt_run.o invsqrt.o -lhexagon

all: invsqrt.out

clean:
	rm -rf *.o invsqrt.h *~ *.out *.bc *.ll *.s *.txt pmu* *.html *.iss.0 out.bin result run.std* pa_dump.core.0 stats_dump.v60.iss.0

.PHONY: assembly

assembly: invsqrt.cpp
	${CXX}  invsqrt.cpp -g  -o invsqrt.generate.out -O0 -DASSEMBLY --std=c++11 ${INCLUDES} ${LDFLAGS} ${EXTRA_FLAGS} -DLOG2VLEN=${LOG2VLEN} ${VERSION_FLAG}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./invsqrt.generate.out ${B128_MODE}

.PHONY: bitcode

bitcode: invsqrt.cpp
	${CXX}  invsqrt.cpp -g  -lHalide -lpthread -ldl -o invsqrt.generate.out -O0 -DBITCODE --std=c++11 ${INCLUDES} ${LDFLAGS} ${EXTRA_FLAGS} -DLOG2VLEN=${LOG2VLEN} ${VERSION_FLAG}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./invsqrt.generate.out ${B128_MODE}

.PHONY: all run bmark-report

all: invsqrt.out run bmark-report
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

run: invsqrt.out
	${HEX_TOOLS}/bin/hexagon-sim invsqrt.out ${TIMING} -- ${ARG12} ${HALIDE_TOP}/test/hexagon/apps/testvectors/football1920x1080.bin out.bin 1>${CURR_DIR}/run.stdout 2>${CURR_DIR}/run.stderr; \
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

stmt: invsqrt.cpp
	${CXX}  invsqrt.cpp -g  -lHalide -lpthread -ldl -o invsqrt.generate.out -O0 -DSTMT --std=c++11 ${INCLUDES} ${LDFLAGS} ${EXTRA_FLAGS} -DLOG2VLEN=${LOG2VLEN} ${VERSION_FLAG}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./invsqrt.generate.out

.PHONY: synthetic

invsqrt_run.synthetic.o: invsqrt.o invsqrt.h invsqrt_run.cpp
	${HEXAGON_CLANG} -c invsqrt_run.cpp ${CFLAGS} -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include -DSYNTHETIC -o invsqrt_run.synthetic.o ${CXXFLAGS}


invsqrt.synthetic.out: invsqrt_run.synthetic.o invsqrt.o invsqrt.h
	${HEXAGON_CLANG} -mv60 -o invsqrt.synthetic.out invsqrt_run.synthetic.o invsqrt.o -lhexagon


synthetic: invsqrt.synthetic.out
	${HEX_TOOLS}/bin/hexagon-sim invsqrt.synthetic.out ${TIMING}
