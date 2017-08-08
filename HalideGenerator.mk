# ------------------------------------------------------------------------------
#
# General rules for building Generators. These are targeted at the
# Generators in test/generator, but can (and should) be generalized elsewhere.
#
# These variables are chosen to allow for Target-Specific Variable Values to 
# override part(s) of the Generator ruleset, without having to make error-prone
# near-duplicates of the build rules for things that need minor customization.
#
# To use:
# -- Use vpath to specify the directories containing your _generator.cpp files:
# 
#   vpath %_generator.cpp dir1/,dir2/,etc
#
# -- define the variables listed as "must-define" below
# -- include this file
#
#
# For every foo_generator.cpp file found, we produce:
# -- foo.generator (the compiled Generator executable)
# -- foo.a (the AOT-compiled output of the Generator)
# -- foo.h (the corresponding header file)
# -- foo.cpp (the C++ version of the output of the Generator)
# -- foo.rungen (foo.a linked with the RunGen wrapper)
#
#
# You can customize various settings by adding target-specific variable definitions,
# e.g. to specify that foo should be built with user_context and cuda, use
#
#   $(GENERATOR_FILTERS_DIR)/foo.a: GENERATOR_EXTRA_FEATURES=user_context-cuda


# Note that several of these need SECONDEXPANSION enabled to work.
.SECONDEXPANSION:

# These are the variables you *must* define
CXX                           ?= error-must-define-CXX
GENERATOR_HALIDE_INCLUDES_DIR ?= error-must-define-GENERATOR_HALIDE_INCLUDES_DIR
GENERATOR_HALIDE_TOOLS_DIR    ?= error-must-define-GENERATOR_HALIDE_TOOLS_DIR
GENERATOR_LIBHALIDE_PATH      ?= error-must-define-GENERATOR_LIBHALIDE_PATH
GENERATOR_TARGET 	          ?= error-must-define-GENERATOR_TARGET
GENERATOR_BIN_DIR 	          ?= error-must-define-GENERATOR_BIN_DIR

GENERATOR_BUILD_DIR   		  = $(GENERATOR_BIN_DIR)/build
GENERATOR_FILTERS_DIR 		  = $(GENERATOR_BIN_DIR)/$(GENERATOR_TARGET)/build

GENERATOR_CXX_FLAGS           ?=
GENERATOR_GENERATOR_LD_FLAGS  ?=

GENERATOR_HALIDE_H_PATH       ?= $(GENERATOR_HALIDE_INCLUDES_DIR)/Halide.h

GENERATOR_IMAGE_IO_LIBS       ?= 
GENERATOR_IMAGE_IO_CXX_FLAGS  ?= -DHALIDE_NO_PNG -DHALIDE_NO_JPEG

# Default features to add to the Generator's Halide target.
GENERATOR_EXTRA_FEATURES ?=

# Default function name for Generator AOT rules (empty = based on Generator name).
GENERATOR_FUNCNAME ?= $(notdir $*)

# Default Generator args for Generator AOT rules (if any)
GENERATOR_ARGS ?=

# Generator name to use (empty = assume only one Generator present)
GENERATOR_GENERATOR_NAME ?=

# Extra deps that are required when building the Generator itself (if any)
GENERATOR_GENERATOR_DEPS ?=

# Extra deps that are required when linking the filter (if any).
GENERATOR_FILTER_DEPS ?=

# The Generator to use to produce a target; usually %.generator,
# but can vary when we produces multiple different filters
# from the same Generator (by changing target, generator_args, etc)
GENERATOR_GENERATOR_EXECUTABLE=$(GENERATOR_BIN_DIR)/$*.generator

GENERATOR_RUNTIME_LIB = $(GENERATOR_FILTERS_DIR)/runtime.a

# ------------------------------------------------------------------------------

$(GENERATOR_BUILD_DIR)/GenGen.o: $(GENERATOR_HALIDE_TOOLS_DIR)/GenGen.cpp $(GENERATOR_HALIDE_INCLUDES_DIR)/Halide.h
	@mkdir -p $(@D)
	$(CXX) -c $< $(GENERATOR_CXX_FLAGS) -I$(GENERATOR_HALIDE_INCLUDES_DIR) -o $@

# ------------------------------------------------------------------------------

# By default, %.generator is produced by building %_generator.cpp & linking with GenGen.cpp
$(GENERATOR_BUILD_DIR)/%_generator.o: %_generator.cpp $(GENERATOR_HALIDE_INCLUDES_DIR)/Halide.h
	@mkdir -p $(@D)
	$(CXX) $(GENERATOR_CXX_FLAGS) -I$(GENERATOR_HALIDE_INCLUDES_DIR) -I$(GENERATOR_FILTERS_DIR) -c $< -o $@

$(GENERATOR_BIN_DIR)/%.generator: $(GENERATOR_BUILD_DIR)/GenGen.o $(GENERATOR_LIBHALIDE_PATH) $(GENERATOR_HALIDE_H_PATH) $(GENERATOR_BUILD_DIR)/%_generator.o $$(GENERATOR_GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(filter %.cpp %.o %.a,$^) $(GENERATOR_GENERATOR_LD_FLAGS) -o $@

# Don't automatically delete Generators, since we may invoke the same one multiple
# times with different arguments.
# (Really, .SECONDARY is what we want, but it won't accept wildcards)
.PRECIOUS: $(GENERATOR_BIN_DIR)/%.generator

GENERATOR_TARGET_WITH_FEATURES ?= $(GENERATOR_TARGET)-no_runtime$(if $(GENERATOR_EXTRA_FEATURES),-,)$(GENERATOR_EXTRA_FEATURES)

# By default, %.a/.h/.cpp are produced by executing %.generator. Runtimes are not included in these.
$(GENERATOR_FILTERS_DIR)/%.a: $$(GENERATOR_GENERATOR_EXECUTABLE) $$(GENERATOR_FILTER_DEPS)
	@mkdir -p $(@D)
	$< -e static_library,h,cpp -g "$(GENERATOR_GENERATOR_NAME)" -f "$(GENERATOR_FUNCNAME)" -n $* -o $(GENERATOR_FILTERS_DIR) target=$(GENERATOR_TARGET_WITH_FEATURES) $(GENERATOR_ARGS)
	if [ -n "$(GENERATOR_FILTER_DEPS)" ]; then $(GENERATOR_HALIDE_TOOLS_DIR)/makelib.sh $@ $@ $(GENERATOR_FILTER_DEPS); fi

$(GENERATOR_FILTERS_DIR)/%.h: $(GENERATOR_FILTERS_DIR)/%.a
	@ # @echo $@ produced implicitly by $^

$(GENERATOR_FILTERS_DIR)/%.cpp: $(GENERATOR_FILTERS_DIR)/%.a
	@ # @echo $@ produced implicitly by $^

$(GENERATOR_FILTERS_DIR)/%.stub.h: $(GENERATOR_BIN_DIR)/%.generator
	@mkdir -p $(@D)
	$< -n $* -o $(GENERATOR_FILTERS_DIR) -e cpp_stub

# ------------------------------------------------------------------------------
# Make an empty generator for generating runtimes.
$(GENERATOR_BIN_DIR)/runtime.rgenerator: $(GENERATOR_BUILD_DIR)/GenGen.o $(GENERATOR_LIBHALIDE_PATH) $(GENERATOR_HALIDE_H_PATH)
	@mkdir -p $(@D)
	$(CXX) $(filter-out %.h,$^) $(GENERATOR_GENERATOR_LD_FLAGS) -o $@

# Generate a standalone runtime for a given target string
# Note that this goes into GENERATOR_FILTERS_DIR, but we define the rule
# this way so that things downstream can just depend on the right path to
# force generation of custom runtimes.
$(GENERATOR_BIN_DIR)/%/build/runtime.a: $(GENERATOR_BIN_DIR)/runtime.rgenerator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=$*

# ------------------------------------------------------------------------------

# Rules for the "RunGen" utility, which lets most Generators run via
# standard command-line tools.

$(GENERATOR_BUILD_DIR)/RunGen.o: $(GENERATOR_HALIDE_TOOLS_DIR)/RunGen.cpp $(GENERATOR_HALIDE_INCLUDES_DIR)/HalideRuntime.h
	@mkdir -p $(@D)
	$(CXX) -c $< $(GENERATOR_CXX_FLAGS) $(GENERATOR_IMAGE_IO_CXX_FLAGS) -I$(GENERATOR_HALIDE_INCLUDES_DIR) -I$(GENERATOR_HALIDE_TOOLS_DIR) -o $@

$(GENERATOR_BIN_DIR)/%.rungen: $(GENERATOR_BUILD_DIR)/RunGen.o $(GENERATOR_FILTERS_DIR)/runtime.a $(GENERATOR_HALIDE_TOOLS_DIR)/RunGenStubs.cpp $(GENERATOR_FILTERS_DIR)/%.a
	$(CXX) -std=c++11 -DHL_RUNGEN_FILTER_HEADER=\"$*.h\" -I$(GENERATOR_FILTERS_DIR) $^ $(GENERATOR_IMAGE_IO_CXX_FLAGS) $(GENERATOR_IMAGE_IO_LIBS) -o $@

# Don't automatically delete RunGen, since we may invoke the same one multiple times with different arguments.
# (Really, .SECONDARY is what we want, but it won't accept wildcards)
.PRECIOUS: $(GENERATOR_BIN_DIR)/%.rungen

RUNARGS ?=

$(GENERATOR_BIN_DIR)/%.run: $(GENERATOR_BIN_DIR)/%.rungen
	$< $(RUNARGS)

clean_generators:
	rm -f $(GENERATOR_BUILD_DIR)/GenGen.o
	rm -f $(GENERATOR_BUILD_DIR)/RunGen.o
	rm -rf $(GENERATOR_BIN_DIR)/$(GENERATOR_TARGET)/generator_*
	rm -rf $(GENERATOR_BIN_DIR)/*.generator
	rm -rf $(GENERATOR_BIN_DIR)/*.rungen
	rm -rf $(GENERATOR_BIN_DIR)/*/build/runtime.a
	rm -rf $(GENERATOR_BUILD_DIR)/*_generator.o
	rm -rf $(GENERATOR_FILTERS_DIR)
