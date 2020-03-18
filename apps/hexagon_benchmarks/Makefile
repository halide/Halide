include ../support/Makefile.inc


FILTERS ?= conv3x3a16 dilate3x3 median3x3 gaussian5x5 sobel conv3x3a32

ITERATIONS ?= 10

OBJS = $(patsubst %,$(BIN)/\%/%.o, $(FILTERS))

LINK_OBJS = $(patsubst %,$(BIN)/$$*/%.o, $(FILTERS))

UPPERCASE_FILTERS = $(shell echo $(FILTERS) | tr '[:lower:]' '[:upper:]')
DASH_D_DEFINES = $(patsubst %, -D%=1, $(UPPERCASE_FILTERS))

PARALLEL_SCHED ?= true
PREFETCH_SCHED ?= true
SCHEDULING_OPTS = use_parallel_sched=${PARALLEL_SCHED} use_prefetch_sched=${PREFETCH_SCHED}

$(GENERATOR_BIN)/%.generator : %_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/conv3x3a16.o: $(GENERATOR_BIN)/conv3x3.generator
	@mkdir -p $(@D)
	$^ -g conv3x3 -o $(@D) -e object,c_header -f conv3x3a16 target=$* accumulator_type=int16 ${SCHEDULING_OPTS}

$(BIN)/%/dilate3x3.o: $(GENERATOR_BIN)/dilate3x3.generator
	@mkdir -p $(@D)
	$^ -g dilate3x3 -o $(@D) -e object,c_header -f dilate3x3 target=$* ${SCHEDULING_OPTS}

$(BIN)/%/median3x3.o: $(GENERATOR_BIN)/median3x3.generator
	@mkdir -p $(@D)
	$^ -g median3x3 -o $(@D) -e object,c_header -f median3x3 target=$* ${SCHEDULING_OPTS}

$(BIN)/%/gaussian5x5.o: $(GENERATOR_BIN)/gaussian5x5.generator
	@mkdir -p $(@D)
	$^ -g gaussian5x5 -o $(@D) -e object,c_header -f gaussian5x5 target=$* ${SCHEDULING_OPTS}

$(BIN)/%/sobel.o: $(GENERATOR_BIN)/sobel.generator
	@mkdir -p $(@D)
	$^ -g sobel -o $(@D) -e object,c_header -f sobel target=$* ${SCHEDULING_OPTS}

$(BIN)/%/conv3x3a32.o: $(GENERATOR_BIN)/conv3x3.generator
	@mkdir -p $(@D)
	$^ -g conv3x3 -o $(@D) -e object,c_header -f conv3x3a32 target=$* accumulator_type=int32 ${SCHEDULING_OPTS}

$(BIN)/%/filters.a : $(OBJS)
	ar q $(BIN)/$*/filters.a $^

$(BIN)/%/process: process.cpp $(BIN)/%/filters.a process.h
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$*  $(DASH_D_DEFINES) -Wall -O0 -g process.cpp $(BIN)/$*/filters.a -o $(@D)/process $(LDFLAGS-$*)

test: $(BIN)/$(HL_TARGET)/process
	$< -n $(ITERATIONS)

clean:
	rm -rf $(BIN)
