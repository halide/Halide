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

TIMING="--timing"
ARG12="1920 1080"

GDB=
CXXFLAGS=--std=c++11 -DLOG2VLEN=${LOG2VLEN} ${EXTRA_FLAGS}
HEXAGON_CLANG=${HEX_TOOLS}/bin/hexagon-clang++
X86_CLANG=/pkg/qct/software/llvm/build_tools/llvm34_140306/bin/clang++
LDFLAGS=-L/pkg/qct/software/llvm/build_tools/libc++_140306/lib  -L ${HALIDE_TOP}/bin -Wl,--start-group -lHalide -lpthread -ldl  -lc++ -Wl,--end-group
#LDFLAGS=-L/pkg/qct/software/llvm/build_tools/libc++_140306/lib -L/pkg/qct/software/llvm/build_tools/libedit_tw/lib -L ${HALIDE_TOP}/bin -Wl,--start-group -lHalide -lpthread -ldl  -lc++ -Wl,--end-group
INCLUDES=-I${HALIDE_TOP}/include -I${HALIDE_TOP}/test/hexagon/include -I/pkg/qct/software/llvm/build_tools/libc++_140306/include/c++/v1
CFLAGS:=${MHVX} -mv60 -DLOG2VLEN=${LOG2VLEN} ${EXTRA_FLAGS}


nv12torgb888.o nv12torgb888.h: nv12torgb888.cpp
	${X86_CLANG} nv12torgb888.cpp -g  -o nv12torgb888.generate.out -O0 -DRUN  ${INCLUDES} ${LDFLAGS} ${CXXFLAGS}
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}
	${GDB} ./nv12torgb888.generate.out


nv12torgb888_run.o: nv12torgb888.o nv12torgb888.h nv12torgb888_run.cpp
	${HEXAGON_CLANG} -c nv12torgb888_run.cpp ${CFLAGS} -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include

nv12torgb888.out: nv12torgb888_run.o nv12torgb888.o nv12torgb888.h
	${HEX_TOOLS}/bin/hexagon-link -march=hexagon -mcpu=hexagonv60 -o nv12torgb888.out ${HEX_TOOLS}/target/hexagon/lib/v60/crt0_standalone.o ${HEX_TOOLS}/target/hexagon/lib/v60/crt0.o ${HEX_TOOLS}/target/hexagon/lib/v60/init.o -L${HEX_TOOLS}/target/hexagon/lib -L${HEX_TOOLS}/target/hexagon/lib/v60 nv12torgb888_run.o nv12torgb888.o --start-group -lstandalone -lc -lgcc -lhexagon --end-group ${HEX_TOOLS}/target/hexagon/lib/v60/fini.o

clean:
	rm -rf *.o median.h *~ *.out *.bc *.ll *.s *.txt pmu* *.html *.iss.0 out.bin result run.std* pa_dump.core.0 stats_dump.v60.iss.0

.PHONY: assembly

assembly: nv12torgb888.cpp
	${X86_CLANG} nv12torgb888.cpp -g -o nv12torgb888.generate.out -O0 -DASSEMBLY ${INCLUDES} ${LDFLAGS} ${CXXFLAGS}
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}
	./nv12torgb888.generate.out

.PHONY: bitcode

bitcode: nv12torgb888.cpp
	${X86_CLANG} nv12torgb888.cpp -g  -o nv12torgb888.generate.out -O0 -DBITCODE ${INCLUDES} ${LDFLAGS} ${CXXFLAGS}
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}
	./nv12torgb888.generate.out

.PHONY: all

all: nv12torgb888.out run bmark-report

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

run: nv12torgb888.out
	${HEX_TOOLS}/bin/hexagon-sim nv12torgb888.out ${TIMING} -- ${HALIDE_TOP}/test/hexagon/apps/testvectors/nv121920x1080.bin ${ARG12}  out.bin 1>${CURR_DIR}/run.stdout 2>${CURR_DIR}/run.stderr; \
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

stmt: nv12torgb888.cpp
	${X86_CLANG} nv12torgb888.cpp -g  -o nv12torgb888.generate.out -O0 -DSTMT ${INCLUDES} ${LDFLAGS} ${CXXFLAGS}
	export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:${LD_LIBRARY_PATH}
	./nv12torgb888.generate.out

.PHONY: synthetic

nv12torgb888_run.synthetic.o: nv12torgb888.o nv12torgb888.h nv12torgb888_run.cpp
	${HEXAGON_CLANG} -c nv12torgb888_run.cpp ${CFLAGS} -I ${HALIDE_TOP}/include -O0 -g -I ${HALIDE_TOP}/test/hexagon/include -DSYNTHETIC -o nv12torgb888_run.synthetic.o ${CXXFLAGS}


nv12torgb888.synthetic.out: nv12torgb888_run.synthetic.o nv12torgb888.o nv12torgb888.h
	${HEX_TOOLS}/bin/hexagon-link -march=hexagon -mcpu=hexagonv60 -o nv12torgb888.synthetic.out ${HEX_TOOLS}/target/hexagon/lib/v60/crt0_standalone.o ${HEX_TOOLS}/target/hexagon/lib/v60/crt0.o ${HEX_TOOLS}/target/hexagon/lib/v60/init.o -L${HEX_TOOLS}/target/hexagon/lib -L${HEX_TOOLS}/target/hexagon/lib/v60 nv12torgb888_run.synthetic.o nv12torgb888.o --start-group -lstandalone -lc -lgcc -lhexagon --end-group ${HEX_TOOLS}/target/hexagon/lib/v60/fini.o


synthetic: nv12torgb888.synthetic.out
	${HEX_TOOLS}/bin/hexagon-sim nv12torgb888.synthetic.out ${TIMING}
