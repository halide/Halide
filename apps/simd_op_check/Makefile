include ../support/Makefile.inc

CXX-hexagon-32-noos-hvx_128 ?= $(HL_HEXAGON_TOOLS)/bin/hexagon-clang++

CXXFLAGS-hexagon-32-noos-hvx_128 ?= -mhvx -mhvx-length=128B -G0

LDFLAGS-hexagon-32-noos-hvx_128 ?= -L../../src/runtime/hexagon_remote/bin/v60/ -lsim_qurt

all: \
	$(BIN)/driver-host \
	$(BIN)/driver-arm-64-android \
	$(BIN)/driver-arm-32-android \
	$(BIN)/driver-hexagon-32-noos-hvx_128 \

hvx_128: $(BIN)/driver-hexagon-32-noos-hvx_128

arm_32: $(BIN)/driver-arm-32-android
arm_64: $(BIN)/driver-arm-64-android

host: $(BIN)/driver-host

$(BIN)/hexagon-32-noos-%/filters.h:
	@mkdir -p $(@D)
	make -C ../../ bin/correctness_simd_op_check_hvx
	cd $(BIN)/hexagon-32-noos-$* && HL_TARGET=hexagon-32-noos-$* LD_LIBRARY_PATH=../../../../bin:$$LD_LIBRARY_PATH ../../../../bin/correctness_simd_op_check_hvx
	cat $(BIN)/hexagon-32-noos-$*/test_*.h > $(BIN)/hexagon-32-noos-$*/filter_headers.h
	echo "filter filters[] = {" > $(BIN)/hexagon-32-noos-$*/filters.h
	cd $(BIN)/hexagon-32-noos-$*; for f in test_*.h; do n=$${f/.h/}; echo '{"'$${n}'", &'$${n}'},'; done >> filters.h
	echo '{NULL, NULL}};' >> $(BIN)/hexagon-32-noos-$*/filters.h

$(BIN)/%/filters.h:
	@mkdir -p $(@D)
	make -C ../../ bin/correctness_simd_op_check
	cd $(BIN)/$* && HL_TARGET=$* LD_LIBRARY_PATH=../../../../bin:$$LD_LIBRARY_PATH ../../../../bin/correctness_simd_op_check
	cat $(BIN)/$*/test_*.h > $(BIN)/$*/filter_headers.h
	echo "filter filters[] = {" > $(BIN)/$*/filters.h
	cd $(BIN)/$*; for f in test_*.h; do n=$${f/.h/}; echo '{"'$${n}'", &'$${n}'},'; done >> filters.h
	echo '{NULL, NULL}};' >> $(BIN)/$*/filters.h

$(BIN)/driver-%: driver.cpp $(BIN)/%/filters.h
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) -I ../../include $(OPTIMIZE) -I $(BIN)/$* driver.cpp $(BIN)/$*/test_*.o $(BIN)/$*/simd_op_check_runtime.o -o $@ $(LDFLAGS-$*) $(HALIDE_SYSTEM_LIBS)

clean:
	rm -rf $(BIN)
