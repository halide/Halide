
NACL_SDK_ROOT ?= $(HOME)/nacl_sdk/pepper_30

WARNINGS := -Wall -Wswitch-enum
CXXFLAGS := -pthread $(WARNINGS) -I $(NACL_SDK_ROOT)/include

UNAME = $(shell uname)

ifeq ($(UNAME), Linux)
TC ?= $(NACL_SDK_ROOT)/toolchain/linux_x86_newlib
ARM_TC ?= $(NACL_SDK_ROOT)/toolchain/linux_arm_newlib
endif

ifeq ($(UNAME), Darwin)
TC ?= $(NACL_SDK_ROOT)/toolchain/mac_x86_newlib
ARM_TC ?= $(NACL_SDK_ROOT)/toolchain/mac_arm_newlib
endif

LIBDIR_32 = $(NACL_SDK_ROOT)/lib/newlib_x86_32/Release
LIBDIR_64 = $(NACL_SDK_ROOT)/lib/newlib_x86_64/Release
LIBDIR_ARM = $(NACL_SDK_ROOT)/lib/newlib_arm/Release

CXX_64 := $(TC)/bin/x86_64-nacl-g++ -m64 -O3 -Wall -s -Wl,--gc-sections
CXX_32 := $(TC)/bin/i686-nacl-g++ -m32 -O3 -Wall -s -Wl,--gc-sections
CXX_ARM := $(ARM_TC)/bin/arm-nacl-g++ -O3 -Wall -s -Wl,--gc-sections -fstack-protector
LDFLAGS_64 := -L $(LIBDIR_64) -lppapi_cpp -lppapi
LDFLAGS_32 := -L $(LIBDIR_32) -lppapi_cpp -lppapi
LDFLAGS_ARM := -L $(LIBDIR_ARM) -lppapi_cpp -lppapi

DEMOS = game_of_life julia reaction_diffusion reaction_diffusion_2
DEMO_OBJECTS = $(DEMOS:%=%_init) $(DEMOS:%=%_update) $(DEMOS:%=%_render)

OBJECTS_32 = nacl_demos_32.o $(DEMO_OBJECTS:%=%_32.o)
OBJECTS_ARM = nacl_demos_arm.o $(DEMO_OBJECTS:%=%_arm.o)
OBJECTS_64 = nacl_demos_64.o $(DEMO_OBJECTS:%=%_64.o)

all: nacl_demos.zip

nacl_demos.zip: nacl_demos_64.nexe nacl_demos_32.nexe nacl_demos_arm.nexe
	zip nacl_demos.zip nacl_demos.html nacl_demos.nmf nacl_demos_32.nexe nacl_demos_64.nexe nacl_demos_arm.nexe $(DEMOS:%:%_32.s) $(DEMOS:%:%_64.s) $(DEMOS:%:%_arm.s)

nacl_demos_64.nexe: $(OBJECTS_64)
	$(CXX_64) -o $@ -m64 $(OBJECTS_64) $(LDFLAGS_64)

nacl_demos_32.nexe: $(OBJECTS_32)
	$(CXX_32) -o $@ -m32 $(OBJECTS_32) $(LDFLAGS_32)

nacl_demos_arm.nexe: $(OBJECTS_ARM)
	$(CXX_ARM) -o $@ $(OBJECTS_ARM) $(LDFLAGS_ARM)

nacl_demos_64.o: nacl_demos.cpp $(DEMOS:%=%_64.o)
	$(CXX_64) -o $@ -m64 -c $< $(CXXFLAGS)

nacl_demos_32.o: nacl_demos.cpp $(DEMOS:%=%_32.o)
	$(CXX_32) -o $@ -m32 -c $< $(CXXFLAGS)

nacl_demos_arm.o: nacl_demos.cpp $(DEMOS:%=%_arm.o)
	$(CXX_ARM) -o $@ -c $< $(CXXFLAGS)

%_64.o: %.generate
	HL_TARGET=x86-64-sse41-nacl DYLD_LIBRARY_PATH=../../bin LD_LIBRARY_PATH=../../bin ./$<
	mv $*_init.o $*_init_64.o
	mv $*_update.o $*_update_64.o
	mv $*_render.o $*_render_64.o

%_32.o: %.generate
	HL_TARGET=x86-32-sse41-nacl DYLD_LIBRARY_PATH=../../bin LD_LIBRARY_PATH=../../bin ./$<
	mv $*_init.o $*_init_32.o
	mv $*_update.o $*_update_32.o
	mv $*_render.o $*_render_32.o

%_arm.o: %.generate
	HL_TARGET=arm-32-nacl DYLD_LIBRARY_PATH=../../bin LD_LIBRARY_PATH=../../bin ./$<
	mv $*_init.o $*_init_arm.o
	mv $*_update.o $*_update_arm.o
	mv $*_render.o $*_render_arm.o

%.generate: %.cpp
	$(CXX) $< -o $@ -I ../../include -L ../../bin -lHalide -lpthread -ldl

clean:
	rm -f *.generate *.o *.nexe *.s $(DEMOS:%=%.h)
