include ../support/Makefile.inc

BIN ?= bin

all: $(BIN)/host/MatrixMultiply

$(BIN)/MatrixMultiply.generator: MatrixMultiply_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/%/MatrixMultiply.o: $(BIN)/MatrixMultiply.generator
	@mkdir -p $(@D)
	$^ -g MatrixMultiply -o $(BIN)/$* -e o,h -f MatrixMultiply target=$(HL_TARGET)

$(BIN)/%/MatrixMultiply: MatrixMultiply.cpp $(BIN)/%/MatrixMultiply.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 MatrixMultiply.cpp $(BIN)/$*/MatrixMultiply.o -o $(BIN)/$*/MatrixMultiply $(LDFLAGS-$*)

run-host: $(BIN)/host/MatrixMultiply
	./MatrixMultiply.sh $(BIN)/host/MatrixMultiply

clean:
	rm -rf $(BIN)
