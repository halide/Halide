HALIDE_PATH ?= $(realpath ../..)
NACL_SDK_ROOT ?= $(HOME)/nacl_sdk/pepper_49

WARNINGS := -Wall -Wswitch-enum
CXXFLAGS := -pthread $(WARNINGS) -I $(NACL_SDK_ROOT)/include -I $(HALIDE_PATH)/include

UNAME = $(shell uname)

ifeq ($(UNAME), Linux)
TC ?= $(NACL_SDK_ROOT)/toolchain/linux_pnacl
endif

ifeq ($(UNAME), Darwin)
TC ?= $(NACL_SDK_ROOT)/toolchain/mac_pnacl
endif

CXX_64 := $(TC)/bin/x86_64-nacl-clang++ -m64 -O3 -Wall -Wl,--gc-sections
CXX_32 := $(TC)/bin/i686-nacl-clang++ -m32 -O3 -Wall -Wl,--gc-sections
CXX_ARM := $(TC)/bin/arm-nacl-clang++ -O3 -Wall -Wl,--gc-sections
CXX_PNACL := $(TC)/bin/pnacl-clang++ -O3
STRIP_64 := $(TC)/bin/x86_64-nacl-strip
LDFLAGS_64 := -L $(TC)/x86_32-nacl/lib/Release -L $(NACL_SDK_ROOT)/lib/clang-newlib_x86_64/Release -lppapi_cpp -lppapi
LDFLAGS_32 := -L $(TC)/x86_64-nacl/lib/Release -L $(NACL_SDK_ROOT)/lib/clang-newlib_x86_32/Release -lppapi_cpp -lppapi
LDFLAGS_ARM := -L $(TC)/arm-nacl/lib/Release -L $(NACL_SDK_ROOT)/lib/clang-newlib_arm/Release -lppapi_cpp -lppapi
LDFLAGS_PNACL := -L $(NACL_SDK_ROOT)/lib/pnacl/Release -lppapi_cpp -lppapi

DEMOS = game_of_life julia reaction_diffusion reaction_diffusion_2
DEMO_OBJECTS = $(DEMOS:%=%_init) $(DEMOS:%=%_update) $(DEMOS:%=%_render)

OBJECTS_32 = nacl_demos_32.o $(DEMO_OBJECTS:%=build_32/%.a)
OBJECTS_ARM = nacl_demos_arm.o $(DEMO_OBJECTS:%=build_arm/%.a)
OBJECTS_64 = nacl_demos_64.o $(DEMO_OBJECTS:%=build_64/%.a)
OBJECTS_PNACL = nacl_demos_pnacl.bc $(DEMO_OBJECTS:%=build_pnacl/%.a)

all: nacl_demos.zip

nacl_demos.zip: nacl_demos_64.nexe nacl_demos_32.nexe nacl_demos_arm.nexe nacl_demos_pnacl.pexe
	zip nacl_demos.zip nacl_demos.html nacl_demos.nmf nacl_demos_pnacl.html nacl_demos_pnacl.nmf nacl_demos_32.nexe nacl_demos_64.nexe nacl_demos_pnacl.pexe nacl_demos_arm.nexe

nacl_demos_64.nexe: nacl_demos_64.o
	$(CXX_64) -o $@ -m64 $(OBJECTS_64) $(LDFLAGS_64)

nacl_demos_32.nexe: nacl_demos_32.o
	$(CXX_32) -o $@ -m32 $(OBJECTS_32) $(LDFLAGS_32)

nacl_demos_arm.nexe: nacl_demos_arm.o
	$(CXX_ARM) -o $@ $(OBJECTS_ARM) $(LDFLAGS_ARM)

nacl_demos_pnacl.pexe: nacl_demos_pnacl.bc
	$(CXX_PNACL) -o $@ $(OBJECTS_PNACL) $(LDFLAGS_PNACL)
	$(TC)/bin/pnacl-finalize $@

nacl_demos_64.o: nacl_demos.cpp $(DEMOS:%=build_64/%.objects)
	$(CXX_64) -o $@ -m64 -c $< $(CXXFLAGS) -I build_64

nacl_demos_32.o: nacl_demos.cpp $(DEMOS:%=build_32/%.objects)
	$(CXX_32) -o $@ -m32 -c $< $(CXXFLAGS) -I build_32

nacl_demos_arm.o: nacl_demos.cpp $(DEMOS:%=build_arm/%.objects)
	$(CXX_ARM) -o $@ -c $< $(CXXFLAGS) -I build_arm

nacl_demos_pnacl.bc: nacl_demos.cpp $(DEMOS:%=build_pnacl/%.objects)
	$(CXX_PNACL) -o $@ -c $< $(CXXFLAGS) -I build_pnacl

build_64/%.objects: %.generate
	@mkdir -p build_64
	cd build_64; HL_TARGET=x86-64-sse41-nacl DYLD_LIBRARY_PATH=$(HALIDE_PATH)/bin LD_LIBRARY_PATH=$(HALIDE_PATH)/bin ../$<
	@touch build_64/$*.objects

build_32/%.objects: %.generate
	@mkdir -p build_32
	cd build_32; HL_TARGET=x86-32-sse41-nacl DYLD_LIBRARY_PATH=$(HALIDE_PATH)/bin LD_LIBRARY_PATH=$(HALIDE_PATH)/bin ../$<
	@touch build_32/$*.objects

build_arm/%.objects: %.generate
	@mkdir -p build_arm
	cd build_arm; HL_TARGET=arm-32-nacl DYLD_LIBRARY_PATH=$(HALIDE_PATH)/bin LD_LIBRARY_PATH=$(HALIDE_PATH)/bin ../$<
	@touch build_arm/$*.objects

build_pnacl/%.objects: %.generate
	@mkdir -p build_pnacl
	cd build_pnacl; HL_TARGET=pnacl-32-nacl DYLD_LIBRARY_PATH=$(HALIDE_PATH)/bin LD_LIBRARY_PATH=$(HALIDE_PATH)/bin ../$<
	@touch build_pnacl/$*.objects

%.generate: %.cpp
	$(CXX) $< -o $@ -I $(HALIDE_PATH)/include -L $(HALIDE_PATH)/bin -lHalide -lpthread -ldl -lz -std=c++11

clean:
	rm -f *.generate *.o *.a *.nexe *.pexe *.bc *.ll *.s *.zip $(DEMOS:%=%.h) build_32/* build_64/* build_arm/* build_pnacl/*
