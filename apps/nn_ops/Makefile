include ../support/Makefile.inc


all: $(BIN)/$(HL_TARGET)/AveragePool $(BIN)/$(HL_TARGET)/Convolution $(BIN)/$(HL_TARGET)/DepthwiseConvolution $(BIN)/$(HL_TARGET)/Im2col $(BIN)/$(HL_TARGET)/MatrixMultiply $(BIN)/$(HL_TARGET)/MaxPool

$(GENERATOR_BIN)/AveragePool.generator: AveragePool_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/AveragePool.o: $(GENERATOR_BIN)/AveragePool.generator
	@mkdir -p $(@D)
	$^ -g AveragePool -o $(@D) -e o,h -f AveragePool target=$*

$(BIN)/%/AveragePool: AveragePool.cpp $(BIN)/%/AveragePool.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall AveragePool.cpp $(BIN)/$*/AveragePool.o -o $(@D)/AveragePool $(LDFLAGS-$*)

$(GENERATOR_BIN)/Convolution.generator: Convolution_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/Convolution.o: $(GENERATOR_BIN)/Convolution.generator
	@mkdir -p $(@D)
	$^ -g Convolution -o $(@D) -e o,h -f Convolution target=$*

$(BIN)/%/Convolution: Convolution.cpp common_reference.cpp $(BIN)/%/Convolution.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall Convolution.cpp common_reference.cpp $(BIN)/$*/Convolution.o -o $(@D)/Convolution $(LDFLAGS-$*)

$(GENERATOR_BIN)/DepthwiseConvolution.generator: DepthwiseConvolution_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/DepthwiseConvolution.generator
	@mkdir -p $(@D)
	@$< -r runtime -o $(@D) target=$*

$(BIN)/%/DepthwiseConvolution_1.o: $(GENERATOR_BIN)/DepthwiseConvolution.generator
	@mkdir -p $(@D)
	$^ -g DepthwiseConvolution -o $(@D) -e o,h -f DepthwiseConvolution_1 target=$*-no_runtime depth_multiplier=1

$(BIN)/%/DepthwiseConvolution_2.o: $(GENERATOR_BIN)/DepthwiseConvolution.generator
	@mkdir -p $(@D)
	$^ -g DepthwiseConvolution -o $(@D) -e o,h -f DepthwiseConvolution_2 target=$*-no_runtime depth_multiplier=2

$(BIN)/%/DepthwiseConvolution_4.o: $(GENERATOR_BIN)/DepthwiseConvolution.generator
	@mkdir -p $(@D)
	$^ -g DepthwiseConvolution -o $(@D) -e o,h -f DepthwiseConvolution_4 target=$*-no_runtime depth_multiplier=4

$(BIN)/%/DepthwiseConvolution_8.o: $(GENERATOR_BIN)/DepthwiseConvolution.generator
	@mkdir -p $(@D)
	$^ -g DepthwiseConvolution -o $(@D) -e o,h -f DepthwiseConvolution_8 target=$*-no_runtime depth_multiplier=8

$(BIN)/%/DepthwiseConvolution: DepthwiseConvolution.cpp common_reference.cpp $(BIN)/%/DepthwiseConvolution_1.o $(BIN)/%/DepthwiseConvolution_2.o $(BIN)/%/DepthwiseConvolution_4.o $(BIN)/%/DepthwiseConvolution_8.o $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall $^ -o $(@D)/DepthwiseConvolution $(LDFLAGS-$*)

$(GENERATOR_BIN)/Im2col.generator: Im2col_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/Im2col.o: $(GENERATOR_BIN)/Im2col.generator
	@mkdir -p $(@D)
	$^ -g Im2col -o $(@D) -e o,h -f Im2col target=$*

$(BIN)/%/Im2col: Im2col.cpp $(BIN)/%/Im2col.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall Im2col.cpp $(BIN)/$*/Im2col.o -o $(@D)/Im2col $(LDFLAGS-$*)

$(GENERATOR_BIN)/MatrixMultiply.generator: MatrixMultiply_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/MatrixMultiply.o: $(GENERATOR_BIN)/MatrixMultiply.generator
	@mkdir -p $(@D)
	$^ -g MatrixMultiply -o $(@D) -e o,h -f MatrixMultiply target=$*

$(BIN)/%/MatrixMultiply: MatrixMultiply.cpp common_reference.cpp $(BIN)/%/MatrixMultiply.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall MatrixMultiply.cpp common_reference.cpp $(BIN)/$*/MatrixMultiply.o -o $(@D)/MatrixMultiply $(LDFLAGS-$*)

$(GENERATOR_BIN)/MaxPool.generator: MaxPool_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/MaxPool.o: $(GENERATOR_BIN)/MaxPool.generator
	@mkdir -p $(@D)
	$^ -g MaxPool -o $(@D) -e o,h -f MaxPool target=$*

$(BIN)/%/MaxPool: MaxPool.cpp $(BIN)/%/MaxPool.o
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall MaxPool.cpp $(BIN)/$*/MaxPool.o -o $(@D)/MaxPool $(LDFLAGS-$*)

run: $(BIN)/$(HL_TARGET)/AveragePool $(BIN)/$(HL_TARGET)/DepthwiseConvolution $(BIN)/$(HL_TARGET)/Convolution $(BIN)/$(HL_TARGET)/Im2col $(BIN)/$(HL_TARGET)/MatrixMultiply $(BIN)/$(HL_TARGET)/MaxPool
	./AveragePool.sh $(BIN)/$(HL_TARGET)/AveragePool
	./Convolution.sh $(BIN)/$(HL_TARGET)/Convolution
	./DepthwiseConvolution.sh $(BIN)/$(HL_TARGET)/DepthwiseConvolution
	./Im2col.sh $(BIN)/$(HL_TARGET)/Im2col
	./MatrixMultiply.sh $(BIN)/$(HL_TARGET)/MatrixMultiply
	./MaxPool.sh $(BIN)/$(HL_TARGET)/MaxPool

test: run

clean:
	rm -rf $(BIN)
