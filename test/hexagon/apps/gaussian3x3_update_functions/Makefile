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
TEST_TYPE?=realimage
CFLAGS:=${MHVX} -mv60 -DLOG2VLEN=${LOG2VLEN}

gaussian3x3.o gaussian3x3.h: gaussian3x3.cpp
	${CXX}  gaussian3x3.cpp -std=c++11 -g -o gaussian3x3.generate.out -DRUN -O0 ${INCLUDES} ${LDFLAGS} -DLOG2VLEN=${LOG2VLEN}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./gaussian3x3.generate.out


gaussian3x3_run.o: gaussian3x3.o gaussian3x3.h gaussian3x3_run.cpp
	${HEXAGON_CLANG} -c gaussian3x3_run.cpp ${CFLAGS} -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include ${CXXFLAGS}

gaussian3x3.out: gaussian3x3_run.o gaussian3x3.o gaussian3x3.h
	${HEXAGON_CLANG}  -mv60 -o gaussian3x3.out gaussian3x3_run.o gaussian3x3.o -lhexagon -L${HALIDE_TOP}/tools/sim_qurt -lsim_qurt

all: gaussian3x3.out

clean:
	rm -rf *.o gaussian3x3.h *~ *.out *.bc *.ll *.s *.txt pmu* *.html *.iss.0 pa_dump.core.0 stats_dump.v60.iss.0 out.bin result run.std*; \
        $(MAKE) -C ref clean

.PHONY: assembly

assembly: gaussian3x3.cpp
	${CXX}  -stdlib=libc++ gaussian3x3.cpp -std=c++11 -g -o gaussian3x3.generate.out -DASSEMBLY -O0 ${INCLUDES} ${LDFLAGS} -DLOG2VLEN=${LOG2VLEN}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./gaussian3x3.generate.out

.PHONY: bitcode

bitcode: gaussian3x3.cpp
	${CXX}  -stdlib=libc++ gaussian3x3.cpp -std=c++11 -g -o gaussian3x3.generate.out -DBITCODE -O0 ${INCLUDES} ${LDFLAGS} -DLOG2VLEN=${LOG2VLEN}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./gaussian3x3.generate.out

.PHONY: all

all: gaussian3x3.out run bmark-report

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



run: gaussian3x3.out
	${HEX_TOOLS}/bin/hexagon-sim gaussian3x3.out ${TIMING} -- 1920 1080 ${HALIDE_TOP}/test/hexagon/apps/testvectors/football1920x1080.bin out.bin 1>${CURR_DIR}/run.stdout 2>${CURR_DIR}/run.stderr; \
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
	  exit 1; \
        fi





.PHONY: stmt

stmt: gaussian3x3.cpp
	${CXX}  -stdlib=libc++ gaussian3x3.cpp -std=c++11 -g -o gaussian3x3.generate.out -DSTMT -O0 ${INCLUDES} ${LDFLAGS} -DLOG2VLEN=${LOG2VLEN}; \
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}; ./gaussian3x3.generate.out

.PHONY: halide_synthetic

gaussian3x3_run.halide_synthetic.o: gaussian3x3.o gaussian3x3.h gaussian3x3_run.cpp
	${HEXAGON_CLANG} -c gaussian3x3_run.cpp ${CFLAGS} -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include -DSYNTHETIC -o gaussian3x3_run.halide_synthetic.o


gaussian3x3.halide_synthetic.out: gaussian3x3_run.halide_synthetic.o gaussian3x3.o gaussian3x3.h
	${HEXAGON_CLANG} -mv60 -o gaussian3x3.halide_synthetic.out gaussian3x3_run.halide_synthetic.o gaussian3x3.o -lhexagon


halide_synthetic: gaussian3x3.halide_synthetic.out
	${HEX_TOOLS}/bin/hexagon-sim gaussian3x3.halide_synthetic.out ${TIMING}



.PHONY: ref ref_asm ref_int

ref_asm:
	$(MAKE) -C ref -f Makefile.${TEST_TYPE} clean all BUILD=ASM HALIDE_TOP=${CURR_DIR}/../../../..


ref_int:
	$(MAKE) -C ref -f Makefile.${TEST_TYPE} clean all BUILD=INT HALIDE_TOP=${CURR_DIR}/../../../..

ref: ref_asm ref_int
