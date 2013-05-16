
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

CXX_64 := $(TC)/bin/x86_64-nacl-g++ -m64 -O3
CXX_32 := $(TC)/bin/i686-nacl-g++ -m32 -O3
LDFLAGS_64 := -L $(TC)/x86_64-nacl/lib64 -lppapi_cpp -lppapi -lpthread
LDFLAGS_32 := -L $(TC)/x86_64-nacl/lib32 -lppapi_cpp -lppapi -lpthread

OBJECTS_32 = hello_nacl_32.o halide_game_of_life_32.o c_game_of_life_32.o
OBJECTS_64 = hello_nacl_64.o halide_game_of_life_64.o c_game_of_life_64.o

all: hello_nacl.zip

hello_nacl.zip: hello_nacl_64.nexe hello_nacl_32.nexe
	cat hello_nacl.nmf  | grep "lib../" | cut -d'"' -f6 | zip hello_nacl.zip -@
	zip hello_nacl.zip hello_nacl.html hello_nacl.nmf hello_nacl_32.nexe hello_nacl_64.nexe *game_of_life.cpp halide_game_of_life.s

hello_nacl_64.nexe: $(OBJECTS_64) .lib64_linked
	$(CXX_64) -o $@ -m64 $(OBJECTS_64) $(LDFLAGS_64)	

hello_nacl_32.nexe: $(OBJECTS_32) .lib32_linked
	$(CXX_32) -o $@ -m32 $(OBJECTS_32) $(LDFLAGS_32)	

.lib64_linked:
	ln -fs $(TC)/x86_64-nacl/lib64 lib64
	touch .lib64_linked

.lib32_linked:
	ln -fs $(TC)/x86_64-nacl/lib32 lib32
	touch .lib32_linked	

hello_nacl_64.o: hello_nacl.cpp halide_game_of_life_64.o 
	$(CXX_64) -o $@ -m64 -c $< $(CXXFLAGS)

hello_nacl_32.o: hello_nacl.cpp halide_game_of_life_32.o
	$(CXX_32) -o $@ -m32 -c $< $(CXXFLAGS)

c_game_of_life_32.o: c_game_of_life.cpp
	$(CXX_32) -o $@ -m32 -c $< $(CXXFLAGS)

c_game_of_life_64.o: c_game_of_life.cpp
	$(CXX_64) -o $@ -m64 -c $< $(CXXFLAGS)

halide_game_of_life_64.o: halide_game_of_life
	HL_TARGET=x86-nacl DYLD_LIBRARY_PATH=../../bin LD_LIBRARY_PATH=../../bin ./halide_game_of_life
	mv halide_game_of_life.o halide_game_of_life_64.o

halide_game_of_life_32.o: halide_game_of_life
	HL_TARGET=x86-32-sse41-nacl DYLD_LIBRARY_PATH=../../bin LD_LIBRARY_PATH=../../bin ./halide_game_of_life
	mv halide_game_of_life.o halide_game_of_life_32.o

halide_game_of_life: halide_game_of_life.cpp
	g++ halide_game_of_life.cpp -o halide_game_of_life -I ../../include -L ../../bin -lHalide -lpthread -ldl

clean:
	rm -f halide_game_of_life *.o *.nexe *.s .lib64_linked lib64 .lib32_linked lib32
