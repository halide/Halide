include ../support/Makefile.inc

CXX-hexagon-32-noos-hvx_128 ?= $(HL_HEXAGON_TOOLS)/bin/hexagon-clang++
CXX-xtensa ?= clang++

CXXFLAGS-hexagon-32-noos-hvx_128 ?= -mhvx -mhvx-length=128B -G0
CXXFLAGS-xtensa ?= -std=c++17 -I$(CSTUB_INCLUDE_PATH) -D XCHAL_VISION_TYPE=7 -D XCHAL_VISION_SIMD16=32 -D XCHAL_DATA_WIDTH=64

LDFLAGS-hexagon-32-noos-hvx_128 ?= -L../../src/runtime/hexagon_remote/bin/v60/ -lsim_qurt
LDFLAGS-xtensa ?= -lpthread -ldl

all: \
	$(BIN)/driver-host \
	$(BIN)/driver-arm-64-android \
	$(BIN)/driver-arm-32-android \
	$(BIN)/driver-hexagon-32-noos-hvx_128 \

hvx_128: $(BIN)/driver-hexagon-32-noos-hvx_128

arm_32: $(BIN)/driver-arm-32-android
arm_64: $(BIN)/driver-arm-64-android

host: $(BIN)/driver-host
xtensa: $(BIN)/driver-xtensa

$(BIN)/hexagon-32-noos-%/filters.h:
	@mkdir -p $(@D)
	make -C ../../ bin/correctness_simd_op_check_hvx
	cd $(BIN)/hexagon-32-noos-$* && HL_TARGET=hexagon-32-noos-$* LD_LIBRARY_PATH=../../../../bin:$$LD_LIBRARY_PATH ../../../../bin/correctness_simd_op_check_hvx
	cat $(BIN)/hexagon-32-noos-$*/test_*.h > $(BIN)/hexagon-32-noos-$*/filter_headers.h
	echo "filter filters[] = {" > $(BIN)/hexagon-32-noos-$*/filters.h
	cd $(BIN)/hexagon-32-noos-$*; for f in test_*.h; do n=$${f/.h/}; echo '{"'$${n}'", &'$${n}'},'; done >> filters.h
	echo '{NULL, NULL}};' >> $(BIN)/hexagon-32-noos-$*/filters.h

$(BIN)/xtensa/filters.h:
	@mkdir -p $(@D)
	make -C ../../ bin/correctness_simd_op_check_xtensa
	cd $(BIN)/xtensa && HL_TARGET=xtensa-32-noos LD_LIBRARY_PATH=../../../../bin:$$LD_LIBRARY_PATH ../../../../bin/correctness_simd_op_check_xtensa
	cat $(BIN)/xtensa/test_*.h > $(BIN)/xtensa/filter_headers.h
	echo "filter filters[] = {" > $(BIN)/xtensa/filters.h
	cd $(BIN)/xtensa; for f in test_*.h; do n=$${f/.h/}; echo '{"'$${n}'", &'$${n}'},'; done >> filters.h
	echo '{NULL, NULL}};' >> $(BIN)/xtensa/filters.h

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

$(BIN)/driver-xtensa: driver.cpp $(BIN)/xtensa/filters.h
	@mkdir -p $(@D)
	$(CXX-xtensa) $(CXXFLAGS-xtensa) -I ../../include $(OPTIMIZE) -I $(BIN)/xtensa driver.cpp $(BIN)/xtensa/test_*.cpp $(BIN)/xtensa/simd_op_check_runtime.o $(CSTUB_LIB_PATH)/libcstub.a -o $@ $(LDFLAGS-xtensa) $(HALIDE_SYSTEM_LIBS)

clean:
	rm -rf $(BIN)
