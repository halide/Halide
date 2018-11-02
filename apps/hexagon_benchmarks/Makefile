include ../support/Makefile.inc

BIN ?= bin

FILTERS ?= conv3x3a16 dilate3x3 median3x3 gaussian5x5 sobel conv3x3a32

ITERATIONS ?= 10

OBJS = $(patsubst %,$(BIN)/\%/%.o, $(FILTERS))

LINK_OBJS = $(patsubst %,$(BIN)/$$*/%.o, $(FILTERS))

UPPERCASE_FILTERS = $(shell echo $(FILTERS) | tr '[:lower:]' '[:upper:]')
DASH_D_DEFINES = $(patsubst %, -D%=1, $(UPPERCASE_FILTERS))

PARALLEL_SCHED ?= true
PREFETCH_SCHED ?= true
SCHEDULING_OPTS = use_parallel_sched=${PARALLEL_SCHED} use_prefetch_sched=${PREFETCH_SCHED}

$(BIN)/%.generator : %_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -O3 -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/conv3x3a16.o: $(BIN)/conv3x3.generator
	@mkdir -p $(@D)
	$^ -g conv3x3 -o $(BIN)/$* -e o,h -f conv3x3a16 target=$(HL_TARGET) accumulator_type=int16 ${SCHEDULING_OPTS}

$(BIN)/%/dilate3x3.o: $(BIN)/dilate3x3.generator
	@mkdir -p $(@D)
	$^ -g dilate3x3 -o $(BIN)/$* -e o,h -f dilate3x3 target=$(HL_TARGET) ${SCHEDULING_OPTS}

$(BIN)/%/median3x3.o: $(BIN)/median3x3.generator
	@mkdir -p $(@D)
	$^ -g median3x3 -o $(BIN)/$* -e o,h -f median3x3 target=$(HL_TARGET) ${SCHEDULING_OPTS}

$(BIN)/%/gaussian5x5.o: $(BIN)/gaussian5x5.generator
	@mkdir -p $(@D)
	$^ -g gaussian5x5 -o $(BIN)/$* -e o,h -f gaussian5x5 target=$(HL_TARGET) ${SCHEDULING_OPTS}

$(BIN)/%/sobel.o: $(BIN)/sobel.generator
	@mkdir -p $(@D)
	$^ -g sobel -o $(BIN)/$* -e o,h -f sobel target=$(HL_TARGET) ${SCHEDULING_OPTS}

$(BIN)/%/conv3x3a32.o: $(BIN)/conv3x3.generator
	@mkdir -p $(@D)
	$^ -g conv3x3 -o $(BIN)/$* -e o,h -f conv3x3a32 target=$(HL_TARGET) accumulator_type=int32 ${SCHEDULING_OPTS}

$(BIN)/%/filters.a : $(OBJS)
	ar q $(BIN)/$*/filters.a $^

$(BIN)/%/process: process.cpp $(BIN)/%/filters.a process.h
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$*  $(DASH_D_DEFINES) -Wall -O0 -g process.cpp $(BIN)/$*/filters.a -o $(BIN)/$*/process $(LDFLAGS-$*)

test: $(BIN)/host/process
	$(BIN)/host/process -n $(ITERATIONS)

clean:
	rm -rf $(BIN)
