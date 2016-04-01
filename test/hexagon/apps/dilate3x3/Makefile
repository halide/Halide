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
CXXFLAGS="--std=c++11"
HEXAGON_CLANG=${HEX_TOOLS}/bin/hexagon-clang++
LDFLAGS=-L ${HALIDE_TOP}/bin -lHalide
INCLUDES=-I${HALIDE_TOP}/include -I${HALIDE_TOP}/test/hexagon/include
TEST_NAME= $(shell basename ${CURR_DIR})
B128_MODE=
GDB=
CFLAGS:=${MHVX} -mv60 -DLOG2VLEN=${LOG2VLEN}
dilate3x3.o dilate3x3.h: dilate3x3.cpp
	${CXX}  dilate3x3.cpp -g -o dilate3x3.generate.out -O0 -DRUN --std=c++11 ${INCLUDES} ${LDFLAGS} -DLOG2VLEN=${LOG2VLEN} ${EXTRA_FLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ${GDB} ./dilate3x3.generate.out  ${B128_MODE}


dilate3x3_run.o: dilate3x3.o dilate3x3.h dilate3x3_run.cpp
	${HEXAGON_CLANG} -c dilate3x3_run.cpp ${CFLAGS} -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include

dilate3x3.out: dilate3x3_run.o dilate3x3.o dilate3x3.h
	${HEXAGON_CLANG} -mv60 -o dilate3x3.out dilate3x3_run.o dilate3x3.o -lhexagon -L${HALIDE_TOP}/tools/sim_qurt -lsim_qurt

all: dilate3x3.out

clean:
	rm -rf *.o dilate3x3.h *~ *.out *.bc *.ll *.s *.txt pmu* *.html *.iss.0 out.bin result run.std* pa_dump.core.0 stats_dump.v60.iss.0

.PHONY: assembly

assembly: dilate3x3.cpp
	${CXX}  dilate3x3.cpp -g  -o dilate3x3.generate.out -O0 -DASSEMBLY --std=c++11 ${INCLUDES} ${LDFLAGS}  -DLOG2VLEN=${LOG2VLEN} ${EXTRA_FLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./dilate3x3.generate.out ${B128_MODE}

.PHONY: bitcode

bitcode: dilate3x3.cpp
	${CXX}  dilate3x3.cpp -g  -lHalide -lpthread -ldl -o dilate3x3.generate.out -O0 -DBITCODE --std=c++11 ${INCLUDES} ${LDFLAGS}  -DLOG2VLEN=${LOG2VLEN} ${EXTRA_FLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./dilate3x3.generate.out ${B128_MODE}

.PHONY: all run bmark-report

all: dilate3x3.out run bmark-report
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

run: dilate3x3.out
	${HEX_TOOLS}/bin/hexagon-sim dilate3x3.out ${TIMING} -- 1920 1080 ${HALIDE_TOP}/test/hexagon/apps/testvectors/football1920x1080.bin out.bin 1>${CURR_DIR}/run.stdout 2>${CURR_DIR}/run.stderr; \
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

stmt: dilate3x3.cpp
	${CXX}  dilate3x3.cpp -g  -lHalide -lpthread -ldl -o dilate3x3.generate.out -O0 -DSTMT --std=c++11 ${INCLUDES} ${LDFLAGS}  -DLOG2VLEN=${LOG2VLEN} ${EXTRA_FLAGS}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./dilate3x3.generate.out

.PHONY: synthetic

dilate3x3_run.synthetic.o: dilate3x3.o dilate3x3.h dilate3x3_run.cpp
	${HEXAGON_CLANG} -c dilate3x3_run.cpp ${CFLAGS} -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include -DSYNTHETIC -o dilate3x3_run.synthetic.o ${CXXFLAGS}


dilate3x3.synthetic.out: dilate3x3_run.synthetic.o dilate3x3.o dilate3x3.h
	${HEXAGON_CLANG} -mv60 -o dilate3x3.synthetic.out dilate3x3_run.synthetic.o dilate3x3.o -lhexagon


synthetic: dilate3x3.synthetic.out
	${HEX_TOOLS}/bin/hexagon-sim dilate3x3.synthetic.out ${TIMING}
