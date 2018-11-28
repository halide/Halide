CXX-host ?= c++

BIN ?= bin

# Use the build/tools/make-standalone-toolchain.sh script inside the
# android ndk to make a standalone toolchain to use for this app.
CXX-arm-64-android ?= $(ANDROID_ARM64_TOOLCHAIN)/bin/aarch64-linux-android-c++
CXX-arm-32-android ?= $(ANDROID_ARM_TOOLCHAIN)/bin/arm-linux-androideabi-c++
CXX-hexagon-32-noos-hvx_64 ?= $(HL_HEXAGON_TOOLS)/bin/hexagon-clang++
CXX-hexagon-32-noos-hvx_128 ?= $(HL_HEXAGON_TOOLS)/bin/hexagon-clang++

CXXFLAGS-arm-64-android ?= -llog -fPIE -pie
CXXFLAGS-arm-32-android ?= -llog -fPIE -pie
CXXFLAGS-hexagon-32-noos-hvx_64 ?= -mhvx -G0
CXXFLAGS-hexagon-32-noos-hvx_128 ?= -mhvx -mhvx-double -G0

LDFLAGS-host ?= -lpthread -ldl
LDFLAGS-hexagon-32-noos-hvx_64 ?= -L../../tools/sim_qurt -lsim_qurt
LDFLAGS-hexagon-32-noos-hvx_128 ?= -L../../tools/sim_qurt -lsim_qurt

all: \
	$(BIN)/driver-host \
	$(BIN)/driver-arm-64-android \
	$(BIN)/driver-arm-32-android \
	$(BIN)/driver-hexagon-32-noos-hvx_64 \
	$(BIN)/driver-hexagon-32-noos-hvx_128 \

$(BIN)/%/filters.h:
	@mkdir -p $(@D)
	make -C ../../ bin/correctness_simd_op_check
	cd $(BIN)/$* && HL_TARGET=$* LD_LIBRARY_PATH=../../../../bin ../../../../bin/correctness_simd_op_check
	cat $(BIN)/$*/test_*.h > $(BIN)/$*/filter_headers.h
	echo "filter filters[] = {" > $(BIN)/$*/filters.h
	cd $(BIN)/$*; for f in test_*.h; do n=$${f/.h/}; echo '{"'$${n}'", &'$${n}'},'; done >> filters.h
	echo '{NULL, NULL}};' >> $(BIN)/$*/filters.h

$(BIN)/driver-%: driver.cpp $(BIN)/%/filters.h
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) -I ../../include -O3 -I $(BIN)/$* driver.cpp $(BIN)/$*/test_*.o $(BIN)/$*/simd_op_check_runtime.o -o $@ $(LDFLAGS-$*) $(HALIDE_SYSTEM_LIBS)

clean:
	rm -rf $(BIN)
