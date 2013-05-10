
NACL_SDK_ROOT ?= $(HOME)/nacl_sdk/pepper_26

WARNINGS := -Wall -Wswitch-enum 
CXXFLAGS := -pthread $(WARNINGS) -I $(NACL_SDK_ROOT)/include

CXX := $(NACL_SDK_ROOT)/toolchain/linux_x86_glibc/bin/x86_64-nacl-g++
LDFLAGS := -L $(NACL_SDK_ROOT)/toolchain/linux_x86_glibc/x86_64-nacl/lib -lppapi_cpp -lppapi -lpthread

OBJECTS = hello_nacl.o halide_game_of_life.o

all: hello_nacl.nexe

hello_nacl.nexe: $(OBJECTS) .lib64_linked
	$(CXX) -o $@ -m64 $(OBJECTS) $(LDFLAGS)	

.lib64_linked:
	ln -fs $(NACL_SDK_ROOT)/toolchain/linux_x86_glibc/x86_64-nacl/lib
	touch .lib64_linked

hello_nacl.o: hello_nacl.cpp
	$(CXX) -o $@ -m64 -c $< $(CXXFLAGS)

halide_game_of_life.o: halide_game_of_life
	HL_TARGET=x86-nacl LD_LIBRARY_PATH=../../bin ./halide_game_of_life

halide_game_of_life: halide_game_of_life.cpp
	g++ halide_game_of_life.cpp -o halide_game_of_life -I ../../include -L ../../bin -lHalide -lpthread -ldl

clean:
	rm -f halide_game_of_life *.o *.nexe *.s .lib64_linked