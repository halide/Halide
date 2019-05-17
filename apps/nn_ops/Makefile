include ../support/Makefile.inc


all: $(BIN)/host/AveragePool $(BIN)/host/Convolution $(BIN)/host/DepthwiseConvolution $(BIN)/host/Im2col $(BIN)/host/MatrixMultiply $(BIN)/host/MaxPool

$(GENERATOR_BIN)/AveragePool.generator: AveragePool_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/AveragePool.o: $(GENERATOR_BIN)/AveragePool.generator
	@mkdir -p $(@D)
	$^ -g AveragePool -o $(BIN)/$* -e o,h -f AveragePool target=$(HL_TARGET)

$(BIN)/%/AveragePool: AveragePool.cpp $(BIN)/%/AveragePool.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall AveragePool.cpp $(BIN)/$*/AveragePool.o -o $(BIN)/$*/AveragePool $(LDFLAGS-$*)

$(GENERATOR_BIN)/Convolution.generator: Convolution_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/Convolution.o: $(GENERATOR_BIN)/Convolution.generator
	@mkdir -p $(@D)
	$^ -g Convolution -o $(BIN)/$* -e o,h -f Convolution target=$(HL_TARGET)

$(BIN)/%/Convolution: Convolution.cpp common_reference.cpp $(BIN)/%/Convolution.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall Convolution.cpp common_reference.cpp $(BIN)/$*/Convolution.o -o $(BIN)/$*/Convolution $(LDFLAGS-$*)

$(GENERATOR_BIN)/DepthwiseConvolution.generator: DepthwiseConvolution_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/DepthwiseConvolution_1.o: $(GENERATOR_BIN)/DepthwiseConvolution.generator
	@mkdir -p $(@D)
	$^ -g DepthwiseConvolution -o $(BIN)/$* -e o,h -f DepthwiseConvolution_1 target=$(HL_TARGET) depth_multiplier=1

$(BIN)/%/DepthwiseConvolution_2.o: $(GENERATOR_BIN)/DepthwiseConvolution.generator
	@mkdir -p $(@D)
	$^ -g DepthwiseConvolution -o $(BIN)/$* -e o,h -f DepthwiseConvolution_2 target=$(HL_TARGET) depth_multiplier=2

$(BIN)/%/DepthwiseConvolution_4.o: $(GENERATOR_BIN)/DepthwiseConvolution.generator
	@mkdir -p $(@D)
	$^ -g DepthwiseConvolution -o $(BIN)/$* -e o,h -f DepthwiseConvolution_4 target=$(HL_TARGET) depth_multiplier=4

$(BIN)/%/DepthwiseConvolution_8.o: $(GENERATOR_BIN)/DepthwiseConvolution.generator
	@mkdir -p $(@D)
	$^ -g DepthwiseConvolution -o $(BIN)/$* -e o,h -f DepthwiseConvolution_8 target=$(HL_TARGET) depth_multiplier=8

$(BIN)/%/DepthwiseConvolution: DepthwiseConvolution.cpp common_reference.cpp $(BIN)/%/DepthwiseConvolution_1.o $(BIN)/%/DepthwiseConvolution_2.o $(BIN)/%/DepthwiseConvolution_4.o $(BIN)/%/DepthwiseConvolution_8.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall DepthwiseConvolution.cpp common_reference.cpp $(BIN)/$*/DepthwiseConvolution_1.o $(BIN)/$*/DepthwiseConvolution_2.o $(BIN)/$*/DepthwiseConvolution_4.o $(BIN)/$*/DepthwiseConvolution_8.o -o $(BIN)/$*/DepthwiseConvolution $(LDFLAGS-$*)

$(GENERATOR_BIN)/Im2col.generator: Im2col_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/Im2col.o: $(GENERATOR_BIN)/Im2col.generator
	@mkdir -p $(@D)
	$^ -g Im2col -o $(BIN)/$* -e o,h -f Im2col target=$(HL_TARGET)

$(BIN)/%/Im2col: Im2col.cpp $(BIN)/%/Im2col.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall Im2col.cpp $(BIN)/$*/Im2col.o -o $(BIN)/$*/Im2col $(LDFLAGS-$*)

$(GENERATOR_BIN)/MatrixMultiply.generator: MatrixMultiply_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/MatrixMultiply.o: $(GENERATOR_BIN)/MatrixMultiply.generator
	@mkdir -p $(@D)
	$^ -g MatrixMultiply -o $(BIN)/$* -e o,h -f MatrixMultiply target=$(HL_TARGET)

$(BIN)/%/MatrixMultiply: MatrixMultiply.cpp common_reference.cpp $(BIN)/%/MatrixMultiply.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall MatrixMultiply.cpp common_reference.cpp $(BIN)/$*/MatrixMultiply.o -o $(BIN)/$*/MatrixMultiply $(LDFLAGS-$*)

$(GENERATOR_BIN)/MaxPool.generator: MaxPool_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/MaxPool.o: $(GENERATOR_BIN)/MaxPool.generator
	@mkdir -p $(@D)
	$^ -g MaxPool -o $(BIN)/$* -e o,h -f MaxPool target=$(HL_TARGET)

$(BIN)/%/MaxPool: MaxPool.cpp $(BIN)/%/MaxPool.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall MaxPool.cpp $(BIN)/$*/MaxPool.o -o $(BIN)/$*/MaxPool $(LDFLAGS-$*)

run-host: $(BIN)/host/AveragePool $(BIN)/host/DepthwiseConvolution $(BIN)/host/Convolution $(BIN)/host/Im2col $(BIN)/host/MatrixMultiply $(BIN)/host/MaxPool
	./AveragePool.sh $(BIN)/host/AveragePool
	./Convolution.sh $(BIN)/host/Convolution
	./DepthwiseConvolution.sh $(BIN)/host/DepthwiseConvolution
	./Im2col.sh $(BIN)/host/Im2col
	./MatrixMultiply.sh $(BIN)/host/MatrixMultiply
	./MaxPool.sh $(BIN)/host/MaxPool

test: run-host

clean:
	rm -rf $(BIN)
