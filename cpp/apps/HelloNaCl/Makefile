
NACL_SDK_ROOT ?= $(HOME)/nacl_sdk/pepper_26

WARNINGS := -Wall -Wswitch-enum 
CXXFLAGS := -pthread $(WARNINGS) -I $(NACL_SDK_ROOT)/include

UNAME = $(shell uname)

ifeq ($(UNAME), Linux)
TC ?= $(NACL_SDK_ROOT)/toolchain/linux_x86_glibc
endif

ifeq ($(UNAME), Darwin)
TC ?= $(NACL_SDK_ROOT)/toolchain/mac_x86_glibc
endif

CXX := $(TC)/bin/x86_64-nacl-g++
LDFLAGS := -L $(TC)/x86_64-nacl/lib -lppapi_cpp -lppapi -lpthread

OBJECTS = hello_nacl.o halide_game_of_life.o

all: hello_nacl.nexe

hello_nacl.zip:
	cat hello_nacl.nmf  | grep lib64 | cut -d'"' -f6 | zip hello_nacl.zip -@
	zip hello_nacl.zip hello_nacl.html hello_nacl.nmf hello_nacl.nexe

hello_nacl.nexe: $(OBJECTS) .lib64_linked
	$(CXX) -o $@ -m64 $(OBJECTS) $(LDFLAGS)	

.lib64_linked:
	ln -fs $(TC)/x86_64-nacl/lib lib64
	touch .lib64_linked

hello_nacl.o: hello_nacl.cpp halide_game_of_life.o
	$(CXX) -o $@ -m64 -c $< $(CXXFLAGS)

halide_game_of_life.o: halide_game_of_life
	HL_TARGET=x86-nacl DYLD_LIBRARY_PATH=../../bin LD_LIBRARY_PATH=../../bin ./halide_game_of_life

halide_game_of_life: halide_game_of_life.cpp
	g++ halide_game_of_life.cpp -o halide_game_of_life -I ../../include -L ../../bin -lHalide -lpthread -ldl

clean:
	rm -f halide_game_of_life *.o *.nexe *.s .lib64_linked lib64
