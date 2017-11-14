# - If you are using a Halide distribution, simply set HALIDE_DISTRIB_DIR
# to the path to the distrib directory. 
#
# - More complex usages (mainly, internal-to-Halide users) may, instead, set some combination
# of HALIDE_TOOLS_DIR, HALIDE_INCLUDE_DIR, and HALIDE_COMPILER_LIB. 

HALIDE_DISTRIB_DIR ?= ../../distrib

# If HL_TARGET isn't set, use host
HL_TARGET ?= host

# Default output to bin/target
BIN ?= bin/$(HL_TARGET)

UNAME ?= $(shell uname)

ifeq ($(OS), Windows_NT)
	HALIDE_SHARED_LIBRARY_EXT=dll
else
ifeq ($(UNAME), Darwin)
	HALIDE_SHARED_LIBRARY_EXT=dylib
else
	HALIDE_SHARED_LIBRARY_EXT=so
endif
endif

# Infer other vars from HALIDE_DISTRIB_DIR if not set outright
HALIDE_INCLUDE_DIR ?= $(HALIDE_DISTRIB_DIR)/include
HALIDE_TOOLS_DIR ?= $(HALIDE_DISTRIB_DIR)/tools
# Default to using the shared-library version of libHalide, 
# unless HALIDE_DISTRIB_USE_STATIC_LIBRARY is nonempty:
ifeq ($(HALIDE_DISTRIB_USE_STATIC_LIBRARY), )
	HALIDE_COMPILER_LIB ?= $(HALIDE_DISTRIB_DIR)/lib/libHalide.a
else
	HALIDE_COMPILER_LIB ?= $(HALIDE_DISTRIB_DIR)/bin/libHalide.$(HALIDE_SHARED_LIBRARY_EXT)
endif

ifeq ($(HALIDE_SYSTEM_LIBS), )
# If HALIDE_SYSTEM_LIBS isn't defined, we are compiling against a Halide distribution
# folder; this is normally captured in the halide_config.make file. If that file
# exists in the same directory as this one, just include it here. (If it's not present,
# just fail.)
	include $(HALIDE_DISTRIB_DIR)/halide_config.make
endif

# ----------- C/C++ compiler flags
CXX ?= g++
CFLAGS += -I$(HALIDE_INCLUDE_DIR) -I$(HALIDE_TOOLS_DIR)
CXXFLAGS += -std=c++11 -I$(HALIDE_INCLUDE_DIR) -I$(HALIDE_TOOLS_DIR)
ifeq ($(UNAME), Darwin)
	CXXFLAGS += -fvisibility=hidden
endif

# ----------- Linker flags
LDFLAGS ?= $(HALIDE_SYSTEM_LIBS)

# Automatically add some libraries based on HL_TARGET when it makes sense:
# -- OpenGL
ifneq (, $(findstring opengl,$(HL_TARGET)))
ifeq ($(UNAME), Darwin)
LDFLAGS += -framework OpenGL
else
LDFLAGS += -lGL -lX11
endif
endif

# -- Metal
ifneq (, $(findstring metal,$(HL_TARGET)))
ifeq ($(UNAME), Darwin)
	LDFLAGS += -framework Metal -framework Foundation
endif
endif

# ----------- Generator-specific flags
HALIDE_GENERATOR_DEPS = $(HALIDE_TOOLS_DIR)/GenGen.cpp $(HALIDE_INCLUDE_DIR)/Halide.h $(HALIDE_COMPILER_LIB)
HALIDE_GENERATOR_CXXFLAGS = $(CXXFLAGS) -fno-rtti
HALIDE_GENERATOR_LDFLAGS = $(HALIDE_SYSTEM_LIBS)
ifeq ($(OS), Windows_NT)
	# Flags for mingw environment
	HALIDE_GENERATOR_LDFLAGS += -Wl,--stack,8388608
endif

# ----------- Runtime support (for when building with no_runtime flag)
$(BIN)/runtime.generator: $(HALIDE_GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(HALIDE_GENERATOR_CXXFLAGS) $(filter-out %.h,$^) $(HALIDE_GENERATOR_LDFLAGS) -o $@

$(BIN)/runtime.a: $(BIN)/runtime.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=$(HL_TARGET)

# ----------- halide_image_io.h support: helpers for libpng and libjpeg
LIBPNG_LDFLAGS ?= $(shell libpng-config --ldflags)
LIBPNG_CXXFLAGS ?= $(shell libpng-config --cflags)
# Workaround for libpng-config pointing to 64-bit versions on linux even when we're building for 32-bit
ifneq (,$(findstring -m32,$(CXX)))
ifneq (,$(findstring x86_64,$(LIBPNG_LDFLAGS)))
	LIBPNG_LDFLAGS ?= -lpng
endif
endif

# There's no libjpeg-config, unfortunately. We look for
# jpeglib.h one directory level up from png.h, with various ugly
# special casing to handle issues from OSX Homebrew/MacPorts installs.

LIBJPEG_LINKER_PATH ?= $(shell echo $(LIBPNG_LDFLAGS) | sed -e'/-L.*[/][Cc]ellar[/]libpng/!d;s=\(.*\)/[Cc]ellar/libpng/.*=\1/lib=')
LIBJPEG_LDFLAGS ?= $(LIBJPEG_LINKER_PATH) -ljpeg
LIBJPEG_CXXFLAGS ?= $(shell echo $(filter -I%,$(LIBPNG_CXXFLAGS)) | sed -e'/[Cc]ellar[/]libpng/!s=\(.*\)=\1/..=;s=\(.*\)/[Cc]ellar/libpng/.*=\1/include=')

HALIDE_IMAGE_IO_LDFLAGS = $(LIBPNG_LDFLAGS) $(LIBJPEG_LDFLAGS)
HALIDE_IMAGE_IO_CXXFLAGS = $(LIBPNG_CXXFLAGS) $(LIBJPEG_CXXFLAGS)

# ----------- RunGen support
# Rule patterns to allow RunGen to work with generator targets.
# (Really, we assume that any .a outptu can potentially work with RunGen;
# this will fail for non-generator targets, but that's OK.)
$(BIN)/RunGen.o: $(HALIDE_TOOLS_DIR)/RunGen.cpp
	@mkdir -p $(@D)
	@$(CXX) -c $< $(CXXFLAGS) $(HALIDE_IMAGE_IO_CXXFLAGS) -I$(BIN) -o $@

# Really, .SECONDARY is what we want, but it won't accept wildcards
.PRECIOUS: $(BIN)/%.rungen
$(BIN)/%.rungen: $(BIN)/%.a $(BIN)/RunGen.o $(HALIDE_TOOLS_DIR)/RunGenStubs.cpp
	$(CXX) $(CXXFLAGS) -I$(BIN) -DHL_RUNGEN_FILTER_HEADER=\"$*.h\" $^ -o $@ $(HALIDE_IMAGE_IO_LDFLAGS) $(LDFLAGS)

RUNARGS ?=

# Pseudo target that allows us to build-and-run in one step, e.g.
#
#     make foo.run RUNARGS='input=a output=baz'
#
$(BIN)/%.run: $(BIN)/%.rungen
	@$(CURDIR)/$< $(RUNARGS)
