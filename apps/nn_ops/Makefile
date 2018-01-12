include ../support/Makefile.inc

BIN ?= bin

all: $(BIN)/host/AveragePool $(BIN)/host/Convolution $(BIN)/host/Im2col $(BIN)/host/MatrixMultiply $(BIN)/host/MaxPool

$(BIN)/AveragePool.generator: AveragePool_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/%/AveragePool.o: $(BIN)/AveragePool.generator
	@mkdir -p $(@D)
	$^ -g AveragePool -o $(BIN)/$* -e o,h -f AveragePool target=$(HL_TARGET)

$(BIN)/%/AveragePool: AveragePool.cpp $(BIN)/%/AveragePool.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 AveragePool.cpp $(BIN)/$*/AveragePool.o -o $(BIN)/$*/AveragePool $(LDFLAGS-$*)

$(BIN)/Convolution.generator: Convolution_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/%/Convolution.o: $(BIN)/Convolution.generator
	@mkdir -p $(@D)
	$^ -g Convolution -o $(BIN)/$* -e o,h -f Convolution target=$(HL_TARGET)

$(BIN)/%/Convolution: Convolution.cpp $(BIN)/%/Convolution.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 Convolution.cpp $(BIN)/$*/Convolution.o -o $(BIN)/$*/Convolution $(LDFLAGS-$*)

$(BIN)/Im2col.generator: Im2col_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/%/Im2col.o: $(BIN)/Im2col.generator
	@mkdir -p $(@D)
	$^ -g Im2col -o $(BIN)/$* -e o,h -f Im2col target=$(HL_TARGET)

$(BIN)/%/Im2col: Im2col.cpp $(BIN)/%/Im2col.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 Im2col.cpp $(BIN)/$*/Im2col.o -o $(BIN)/$*/Im2col $(LDFLAGS-$*)

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

run-host: $(BIN)/host/AveragePool $(BIN)/host/Convolution $(BIN)/host/Im2col $(BIN)/host/MatrixMultiply $(BIN)/host/MaxPool
	./AveragePool.sh $(BIN)/host/AveragePool
	./Convolution.sh $(BIN)/host/Convolution
	./Im2col.sh $(BIN)/host/Im2col
	./MatrixMultiply.sh $(BIN)/host/MatrixMultiply
	./MaxPool.sh $(BIN)/host/MaxPool

clean:
	rm -rf $(BIN)
