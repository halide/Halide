include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/filter

test: $(BIN)/$(HL_TARGET)/out.png

$(GENERATOR_BIN)/gaussian_blur.generator: gaussian_blur_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(GENERATOR_BIN)/box_blur.generator: box_blur_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(GENERATOR_BIN)/box_blur_incremental.generator: box_blur_incremental_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/gaussian_blur.a: $(GENERATOR_BIN)/gaussian_blur.generator
	@mkdir -p $(@D)
	$< -g gaussian_blur -f gaussian_blur -e $(GENERATOR_OUTPUTS) -o $(BIN)/$* target=$*-no_runtime 

$(BIN)/%/gaussian_blur_direct.a: $(GENERATOR_BIN)/gaussian_blur.generator
	@mkdir -p $(@D)
	$< -g gaussian_blur_direct -f gaussian_blur_direct -e $(GENERATOR_OUTPUTS) -o $(BIN)/$* target=$*-no_runtime

$(BIN)/%/box_blur.a: $(GENERATOR_BIN)/box_blur.generator
	@mkdir -p $(@D)
	$< -g box_blur -f box_blur -e $(GENERATOR_OUTPUTS) -o $(BIN)/$* target=$*-no_runtime 

$(BIN)/%/box_blur_log.a: $(GENERATOR_BIN)/box_blur.generator
	@mkdir -p $(@D)
	$< -g box_blur_log -f box_blur_log -e $(GENERATOR_OUTPUTS) -o $(BIN)/$* target=$*-no_runtime


$(BIN)/%/box_blur_pyramid_u8.a: $(GENERATOR_BIN)/box_blur.generator
	@mkdir -p $(@D)
	$< -g box_blur_pyramid -f box_blur_pyramid_u8 -e $(GENERATOR_OUTPUTS) -o $(BIN)/$* target=$*-no_runtime input.type=uint8 output.type=uint8

$(BIN)/%/box_blur_pyramid_u16.a: $(GENERATOR_BIN)/box_blur.generator
	@mkdir -p $(@D)
	$< -g box_blur_pyramid -f box_blur_pyramid_u16 -e $(GENERATOR_OUTPUTS) -o $(BIN)/$* target=$*-no_runtime input.type=uint16 output.type=uint16

$(BIN)/%/box_blur_pyramid_f32.a: $(GENERATOR_BIN)/box_blur.generator
	@mkdir -p $(@D)
	$< -g box_blur_pyramid -f box_blur_pyramid_f32 -e $(GENERATOR_OUTPUTS) -o $(BIN)/$* target=$*-no_runtime input.type=float32 output.type=float32

$(BIN)/%/box_blur_incremental.a: $(GENERATOR_BIN)/box_blur_incremental.generator
	@mkdir -p $(@D)
	$< -g box_blur_incremental -f box_blur_incremental -e $(GENERATOR_OUTPUTS) -o $(BIN)/$* target=$*-no_runtime 

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/gaussian_blur.generator 
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

LIBS= \
	$(BIN)/%/gaussian_blur.a \
	$(BIN)/%/gaussian_blur_direct.a \
	$(BIN)/%/box_blur.a \
	$(BIN)/%/box_blur_log.a \
	$(BIN)/%/box_blur_pyramid_u8.a \
	$(BIN)/%/box_blur_pyramid_u16.a \
	$(BIN)/%/box_blur_pyramid_f32.a \
	$(BIN)/%/box_blur_incremental.a \
	$(BIN)/%/runtime.a

.PRECIOUS: $(LIBS)

$(BIN)/%/filter: filter.cpp $(LIBS) 
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	$< gray_big.png $(BIN)/$*/out.png

clean:
	rm -rf $(BIN)
