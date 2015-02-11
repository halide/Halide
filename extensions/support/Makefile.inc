CXX ?= g++
GXX ?= g++
SRCTOP = ../..

# This can be set to -m32 to get a 32-bit build of Halide on a 64-bit system.
# (Normally this can be done via pointing to a compiler that defaults to 32-bits,
#  but that is difficult in some testing situations because it requires having
#  such a compiler handy. One still needs to have 32-bit llvm libraries, etc.)
BUILD_BIT_SIZE ?=
LLVM_CONFIG ?= llvm-config
LLVM_VERSION_TIMES_10 = $(shell $(LLVM_CONFIG) --version | cut -b 1,3)
LLVM_34_OR_OLDER = $(findstring $(LLVM_VERSION_TIMES_10), 32 33 34)
ifneq ($(LLVM_34_OR_OLDER), )
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags)
else
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags --system-libs)
endif

LLVM_CONFIG ?= llvm-config
LLVM_FULL_VERSION = $(shell $(LLVM_CONFIG) --version)
LLVM_LIBDIR = $(shell $(LLVM_CONFIG) --libdir)

HL_TARGET ?= host
HL_JIT_TARGET ?= host

ifeq ($(USE_LLVM_SHARED_LIB), )
LLVM_STATIC_LIBS = -L $(LLVM_LIBDIR) $(shell $(LLVM_CONFIG) --libs bitwriter bitreader linker ipo mcjit $(LLVM_OLD_JIT_COMPONENT) $(X86_LLVM_CONFIG_LIB) $(ARM_LLVM_CONFIG_LIB) $(OPENCL_LLVM_CONFIG_LIB) $(NATIVE_CLIENT_LLVM_CONFIG_LIB) $(PTX_LLVM_CONFIG_LIB) $(AARCH64_LLVM_CONFIG_LIB) $(MIPS_LLVM_CONFIG_LIB))
LLVM_SHARED_LIBS = 
else 
LLVM_STATIC_LIBS = 
LLVM_SHARED_LIBS = -L $(LLVM_LIBDIR) -lLLVM-$(LLVM_FULL_VERSION)
endif

ifdef BUILD_PREFIX
BUILD_DIR = $(SRCTOP)/build/$(BUILD_PREFIX)
BIN_DIR = $(SRCTOP)/bin/$(BUILD_PREFIX)
DISTRIB_DIR = $(SRCTOP)/distrib/$(BUILD_PREFIX)
LIB_HALIDE = $(BIN_DIR)/$(BUILD_PREFIX)/libHalide.a
else
BUILD_DIR = $(SRCTOP)/build
BIN_DIR = $(SRCTOP)/bin
DISTRIB_DIR = $(SRCTOP)/distrib
LIB_HALIDE = $(BIN_DIR)/libHalide.a
endif

# turn on c++11 for llvm 3.5+
CXX11 ?= $(findstring $(LLVM_VERSION_TIMES_10), 35 36 37 38 39 40)

ifneq ($(CXX11),)
LLVM_CXX_FLAGS += -std=c++11
endif

CXX_WARNING_FLAGS = -Wall -Werror -Wno-unused-function -Wcast-qual
CXX_FLAGS = $(CXX_WARNING_FLAGS) -fno-rtti -Woverloaded-virtual -fPIC $(OPTIMIZE) -fno-omit-frame-pointer $(BUILD_BIT_SIZE) \
	-I$(SRCTOP)/src

TEST_CXX_FLAGS ?= $(BUILD_BIT_SIZE) -fno-omit-frame-pointer -fno-rtti -I$(SRCTOP)/include
ifneq ($(CXX11),)
TEST_CXX_FLAGS += -std=c++11
endif
ifeq ($(UNAME), Linux)
TEST_CXX_FLAGS += -rdynamic
HOST_OS=linux
endif

GENGEN_DEPS ?= $(BIN_DIR)/libHalide.so $(SRCTOP)/include/Halide.h $(SRCTOP)/tools/GenGen.cpp
FILTERS_DIR = $(BUILD_DIR)/filters

OBJECTS = $(SOURCE_FILES:%.cpp=$(BUILD_DIR)/%.o)
HEADERS = $(HEADER_FILES:%.h=src/%.h)

STATIC_LIB = $(BIN_DIR)/lib$(EXT_NAME).a
SHARED_LIB = $(BIN_DIR)/lib$(EXT_NAME).so
LIB_HEADER = $(SRCTOP)/include/$(EXT_NAME).h

EXT_LDFLAGS = -L$(BIN_DIR) -lHalide -lz -lpthread -ldl

.PHONY: all
ifneq ($(SOURCE_FILES),)
EXT_LDFLAGS += -l$(EXT_NAME)
all: $(LIB_HEADER) $(STATIC_LIB) $(SHARED_LIB)
else
all: $(LIB_HEADER)
endif

$(STATIC_LIB): $(OBJECTS) $(LIB_HALIDE)
	@-mkdir -p $(BIN_DIR)
	@rm -f $@
	ar q $@ $(OBJECTS) $(LIB_HALIDE)
	ranlib $@

$(SHARED_LIB): $(STATIC_LIB)
	$(CXX) $(BUILD_BIT_SIZE) -shared $(OBJECTS) $(INITIAL_MODULES) $(LLVM_STATIC_LIBS) $(LLVM_LDFLAGS) $(LLVM_SHARED_LIBS) -ldl -lz -lpthread -o $@

$(LIB_HEADER): $(HEADERS) $(BIN_DIR)/build_halide_ext_h
	mkdir -p include
	cd src; ../$(BIN_DIR)/build_halide_ext_h $(HEADER_FILES) > ../$@; cd ..

$(LIB_HALIDE):
	make -C $(SRCTOP) $(LIB_HALIDE)

$(BIN_DIR)/build_halide_ext_h: ../support/build_halide_ext_h.cpp
	g++ $< -o $@

-include $(OBJECTS:.o=.d)

$(BUILD_DIR)/%.o: src/%.cpp src/%.h
	@-mkdir -p $(BUILD_DIR)
	$(CXX) $(CXX_FLAGS) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

.PHONY: clean
clean:
	rm -f $(LIB_HEADER)
	rm -f $(STATIC_LIB)
	rm -f $(SHARED_LIB)
	rm -f $(CORRECTNESS_TESTS:test/correctness/%.cpp=$(BIN_DIR)/test_%)
	rm -f $(PERFORMANCE_TESTS:test/performance/%.cpp=$(BIN_DIR)/performance_%)

LD_PATH_SETUP = DYLD_LIBRARY_PATH=${DYLD_LIBRARY_PATH}:../$(BIN_DIR) LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:../$(BIN_DIR)

CORRECTNESS_TESTS = $(shell ls test/correctness/*.cpp)
PERFORMANCE_TESTS = $(shell ls test/performance/*.cpp)
GENERATOR_TESTS := $(shell ls test/generator/*test.cpp)

.PHONY: run_tests build_tests
.PHONY: test_correctness test_performance test_generators
test_correctness: $(CORRECTNESS_TESTS:test/correctness/%.cpp=test_%)
test_performance: $(PERFORMANCE_TESTS:test/performance/%.cpp=performance_%)
ifneq ($(CXX11),)
test_generators: $(GENERATOR_TESTS:test/generator/%_aottest.cpp=generator_aot_%) $(GENERATOR_TESTS:test/generator/%_jittest.cpp=generator_jit_%)
else
test_generators: ;
endif

ALL_TESTS = test_correctness test_performance

ifneq ($(CXX11),)
ALL_TESTS += test_generators
endif

run_tests: $(ALL_TESTS)

build_tests: $(CORRECTNESS_TESTS:test/correctness/%.cpp=$(BIN_DIR)/test_%) \
	$(PERFORMANCE_TESTS:test/performance/%.cpp=$(BIN_DIR)/performance_%) \

$(BIN_DIR)/test_%: test/correctness/%.cpp $(SHARED_LIB) $(LIB_HEADER)
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) -o $@ -Iinclude $< \
	-L$(BIN_DIR) -lHalide -l$(EXT_NAME) $(LLVM_LDFLAGS)

$(BIN_DIR)/performance_%: test/performance/%.cpp $(SHARED_LIB) $(LIB_HEADER) $(SRCTOP)/test/performance/clock.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) -o $@ -Iinclude -I$(SRCTOP)/test/performance $< \
	-L$(BIN_DIR) -lHalide -l$(EXT_NAME) $(LLVM_LDFLAGS)

# TODO(srj): this doesn't auto-delete, why not?
.INTERMEDIATE: $(FILTERS_DIR)/%.generator

# By default, %.generator is produced by building %_generator.cpp
# Note that the rule includes all _generator.cpp files, so that generators with define_extern
# usage can just add deps later.
$(FILTERS_DIR)/%.generator: test/generator/%_generator.cpp $(GENGEN_DEPS)
	@mkdir -p $(FILTERS_DIR)
	$(CXX) -std=c++11 -g $(CXX_WARNING_FLAGS) -fno-rtti -I$(SRCTOP)/include $(filter %_generator.cpp,$^) $(SRCTOP)/tools/GenGen.cpp -o $@ $(EXT_LDFLAGS)

# By default, %.o/.h are produced by executing %.generator
$(FILTERS_DIR)/%.o $(FILTERS_DIR)/%.h: $(FILTERS_DIR)/%.generator
	@mkdir -p $(FILTERS_DIR)
	@-mkdir -p tmp
	cd tmp; $(LD_PATH_SETUP) ../$< -g $(notdir $*) -o ../$(FILTERS_DIR) target=$(HL_TARGET)

test_%: $(BIN_DIR)/test_%
	@-mkdir -p tmp
	cd tmp ; $(LD_PATH_SETUP) ../$<
	@-echo

performance_%: $(BIN_DIR)/performance_%
	@-mkdir -p tmp
	cd tmp ; $(LD_PATH_SETUP) ../$<
	@-echo

generator_%: $(BIN_DIR)/generator_%
	@-mkdir -p tmp
	cd tmp ; $(LD_PATH_SETUP) ../$<
	@-echo
