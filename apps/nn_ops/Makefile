include ../support/Makefile.inc

BIN ?= bin

all: $(BIN)/host/AveragePool $(BIN)/host/MatrixMultiply $(BIN)/host/MaxPool

$(BIN)/AveragePool.generator: AveragePool_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/%/AveragePool.o: $(BIN)/AveragePool.generator
	@mkdir -p $(@D)
	$^ -g AveragePool -o $(BIN)/$* -e o,h -f AveragePool target=$(HL_TARGET)

$(BIN)/%/AveragePool: AveragePool.cpp $(BIN)/%/AveragePool.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 AveragePool.cpp $(BIN)/$*/AveragePool.o -o $(BIN)/$*/AveragePool $(LDFLAGS-$*)

$(BIN)/MatrixMultiply.generator: MatrixMultiply_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/%/MatrixMultiply.o: $(BIN)/MatrixMultiply.generator
	@mkdir -p $(@D)
	$^ -g MatrixMultiply -o $(BIN)/$* -e o,h -f MatrixMultiply target=$(HL_TARGET)

$(BIN)/%/MatrixMultiply: MatrixMultiply.cpp $(BIN)/%/MatrixMultiply.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 MatrixMultiply.cpp $(BIN)/$*/MatrixMultiply.o -o $(BIN)/$*/MatrixMultiply $(LDFLAGS-$*)

$(BIN)/MaxPool.generator: MaxPool_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/%/MaxPool.o: $(BIN)/MaxPool.generator
	@mkdir -p $(@D)
	$^ -g MaxPool -o $(BIN)/$* -e o,h -f MaxPool target=$(HL_TARGET)

$(BIN)/%/MaxPool: MaxPool.cpp $(BIN)/%/MaxPool.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 MaxPool.cpp $(BIN)/$*/MaxPool.o -o $(BIN)/$*/MaxPool $(LDFLAGS-$*)

run-host: $(BIN)/host/AveragePool $(BIN)/host/MatrixMultiply $(BIN)/host/MaxPool
	./AveragePool.sh $(BIN)/host/AveragePool
	./MatrixMultiply.sh $(BIN)/host/MatrixMultiply
	./MaxPool.sh $(BIN)/host/MaxPool

clean:
	rm -rf $(BIN)
