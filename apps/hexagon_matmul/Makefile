include ../support/Makefile.inc

BIN ?= bin

all: $(BIN)/process-host $(BIN)/process-arm-64-android $(BIN)/process-arm-32-android

$(BIN)/matmul.generator: matmul_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/%/matmul.o: $(BIN)/matmul.generator
	@mkdir -p $(@D)
	$^ -g matmul -o $(BIN)/$* -e o,h -f matmul target=$(HL_TARGET)

$(BIN)/%/process: process.cpp $(BIN)/%/matmul.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process.cpp $(BIN)/$*/matmul.o -o $(BIN)/$*/process $(LDFLAGS-$*)

test: $(BIN)/host/process
	$(BIN)/host/process 10 256 256 256

clean:
	rm -rf $(BIN)
