
NACL_SDK_ROOT ?= $(HOME)/nacl_sdk/pepper_35

WARNINGS := -Wall -Wswitch-enum
CXXFLAGS := -pthread $(WARNINGS) -I $(NACL_SDK_ROOT)/include -I ../../include

UNAME = $(shell uname)

ifeq ($(UNAME), Linux)
TC ?= $(NACL_SDK_ROOT)/toolchain/linux_x86_newlib
ARM_TC ?= $(NACL_SDK_ROOT)/toolchain/linux_arm_newlib
PNACL_TC ?= $(NACL_SDK_ROOT)/toolchain/linux_pnacl
endif

ifeq ($(UNAME), Darwin)
TC ?= $(NACL_SDK_ROOT)/toolchain/mac_x86_newlib
ARM_TC ?= $(NACL_SDK_ROOT)/toolchain/mac_arm_newlib
PNACL_TC ?= $(NACL_SDK_ROOT)/toolchain/mac_pnacl
endif

LIBDIR_32 = $(NACL_SDK_ROOT)/lib/newlib_x86_32/Release
LIBDIR_64 = $(NACL_SDK_ROOT)/lib/newlib_x86_64/Release
LIBDIR_ARM = $(NACL_SDK_ROOT)/lib/newlib_arm/Release
LIBDIR_PNACL = $(NACL_SDK_ROOT)/lib/pnacl/Release

CXX_64 := $(TC)/bin/x86_64-nacl-g++ -m64 -O3 -s -Wl,--gc-sections
CXX_32 := $(TC)/bin/i686-nacl-g++ -m32 -O3 -s -Wl,--gc-sections
CXX_ARM := $(ARM_TC)/bin/arm-nacl-g++ -O3 -s -Wl,--gc-sections
CXX_PNACL := $(PNACL_TC)/bin/pnacl-clang++ -O3
LDFLAGS_64 := -L $(LIBDIR_64) -lppapi_cpp -lppapi
LDFLAGS_32 := -L $(LIBDIR_32) -lppapi_cpp -lppapi
LDFLAGS_ARM := -L $(LIBDIR_ARM) -lppapi_cpp -lppapi
LDFLAGS_PNACL := -L $(LIBDIR_PNACL) -lppapi_cpp -lppapi

OBJECTS_32 = build_32/hello_nacl.o build_32/halide_game_of_life.o build_32/c_game_of_life.o
OBJECTS_64 = build_64/hello_nacl.o build_64/halide_game_of_life.o build_64/c_game_of_life.o
OBJECTS_ARM = build_arm/hello_nacl.o build_arm/halide_game_of_life.o build_arm/c_game_of_life.o
OBJECTS_PNACL = build_pnacl/hello_nacl.bc build_pnacl/halide_game_of_life.bc build_pnacl/c_game_of_life.bc

all: hello_nacl.zip

hello_nacl.zip: hello_nacl_64.nexe hello_nacl_32.nexe hello_nacl_arm.nexe hello_pnacl.pexe
	cp build_32/halide_game_of_life.s halide_game_of_life_32.s
	cp build_64/halide_game_of_life.s halide_game_of_life_64.s
	cp build_arm/halide_game_of_life.s halide_game_of_life_arm.s
	zip hello_nacl.zip hello_nacl.html hello_nacl.nmf hello_pnacl.html hello_pnacl.nmf hello_nacl_32.nexe hello_nacl_64.nexe hello_nacl_arm.nexe hello_pnacl.pexe *game_of_life.cpp halide_game_of_life_32.s halide_game_of_life_arm.s halide_game_of_life_64.s

hello_nacl_64.nexe: $(OBJECTS_64)
	$(CXX_64) -o $@ -m64 $(OBJECTS_64) $(LDFLAGS_64)

hello_nacl_32.nexe: $(OBJECTS_32)
	$(CXX_32) -o $@ -m32 $(OBJECTS_32) $(LDFLAGS_32)

hello_nacl_arm.nexe: $(OBJECTS_ARM)
	$(CXX_ARM) -o $@ $(OBJECTS_ARM) $(LDFLAGS_ARM)

hello_pnacl.pexe: $(OBJECTS_PNACL)
	$(CXX_PNACL) -o $@ $(OBJECTS_PNACL) $(LDFLAGS_PNACL)
	$(PNACL_TC)/bin/pnacl-finalize $@

build_64/hello_nacl.o: hello_nacl.cpp build_64/halide_game_of_life.o
	$(CXX_64) -o $@ -m64 -c $< $(CXXFLAGS) -I build_64

build_32/hello_nacl.o: hello_nacl.cpp build_32/halide_game_of_life.o
	$(CXX_32) -o $@ -m32 -c $< $(CXXFLAGS) -I build_32

build_arm/hello_nacl.o: hello_nacl.cpp build_arm/halide_game_of_life.o
	$(CXX_ARM) -o $@ -c $< $(CXXFLAGS) -I build_arm

build_pnacl/hello_nacl.bc: hello_nacl.cpp build_pnacl/halide_game_of_life.bc
	$(CXX_PNACL) -o $@ -c $< $(CXXFLAGS) -I build_pnacl

build_32/c_game_of_life.o: c_game_of_life.cpp build_32/halide_game_of_life.o
	mkdir -p build_32
	$(CXX_32) -o $@ -m32 -c $< $(CXXFLAGS) -I build_32

build_64/c_game_of_life.o: c_game_of_life.cpp build_64/halide_game_of_life.o
	mkdir -p build_64
	$(CXX_64) -o $@ -m64 -c $< $(CXXFLAGS) -I build_64

build_arm/c_game_of_life.o: c_game_of_life.cpp build_arm/halide_game_of_life.o
	mkdir -p build_arm
	$(CXX_ARM) -o $@ -c $< $(CXXFLAGS) -I build_arm

build_pnacl/c_game_of_life.bc: c_game_of_life.cpp build_pnacl/halide_game_of_life.bc
	mkdir -p build_pnacl
	$(CXX_PNACL) -o $@ -c $< $(CXXFLAGS) -I build_pnacl

build_64/halide_game_of_life.o: halide_game_of_life
	mkdir -p build_64
	cd build_64; \
	HL_TARGET=x86-64-sse41-nacl \
	DYLD_LIBRARY_PATH=../../../bin \
	LD_LIBRARY_PATH=../../../bin \
	../halide_game_of_life

build_32/halide_game_of_life.o: halide_game_of_life
	mkdir -p build_32
	cd build_32; \
	HL_TARGET=x86-32-sse41-nacl \
	DYLD_LIBRARY_PATH=../../../bin \
	LD_LIBRARY_PATH=../../../bin \
	../halide_game_of_life

build_arm/halide_game_of_life.o: halide_game_of_life
	mkdir -p build_arm
	cd build_arm; \
	HL_TARGET=arm-32-nacl \
	DYLD_LIBRARY_PATH=../../../bin \
	LD_LIBRARY_PATH=../../../bin \
	../halide_game_of_life

build_pnacl/halide_game_of_life.bc: halide_game_of_life
	mkdir -p build_pnacl
	cd build_pnacl; \
	HL_TARGET=pnacl-64 \
	DYLD_LIBRARY_PATH=../../../bin \
	LD_LIBRARY_PATH=../../../bin \
	../halide_game_of_life
	mv build_pnacl/halide_game_of_life.o build_pnacl/halide_game_of_life.bc

halide_game_of_life: halide_game_of_life.cpp
	$(CXX) halide_game_of_life.cpp -o halide_game_of_life -I ../../include -L ../../bin -lHalide -lpthread -ldl -lz

clean:
	rm -f halide_game_of_life *.o *.nexe *.bc *.ll *.pexe *.s build_arm/* build_pnacl/* build_32/* build_64/*
