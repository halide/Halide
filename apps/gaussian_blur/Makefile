include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/filter

test: $(BIN)/$(HL_TARGET)/out.png

$(GENERATOR_BIN)/gaussian_blur.generator: gaussian_blur_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/gaussian_blur.a: $(GENERATOR_BIN)/gaussian_blur.generator
	@mkdir -p $(@D)
	$< -g gaussian_blur -f gaussian_blur -e $(GENERATOR_OUTPUTS) -o $(BIN)/$* target=$*-no_runtime auto_schedule=false

$(BIN)/%/gaussian_blur_auto_schedule.a: $(GENERATOR_BIN)/gaussian_blur.generator
	@mkdir -p $(@D)
	$< -g gaussian_blur -f gaussian_blur_auto_schedule -o $(BIN)/$* target=$*-no_runtime #auto_schedule=true

$(BIN)/%/gaussian_blur_direct.a: $(GENERATOR_BIN)/gaussian_blur.generator
	@mkdir -p $(@D)
	$< -g gaussian_blur_direct -f gaussian_blur_direct -e $(GENERATOR_OUTPUTS) -o $(BIN)/$* target=$*-no_runtime auto_schedule=false

$(BIN)/%/gaussian_blur_direct_auto_schedule.a: $(GENERATOR_BIN)/gaussian_blur.generator
	@mkdir -p $(@D)
	$< -g gaussian_blur_direct -f gaussian_blur_direct_auto_schedule -o $(BIN)/$* target=$*-no_runtime #auto_schedule=true

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/gaussian_blur.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

$(BIN)/%/filter: filter.cpp \
	$(BIN)/%/gaussian_blur.a $(BIN)/%/gaussian_blur_auto_schedule.a \
	$(BIN)/%/gaussian_blur_direct.a $(BIN)/%/gaussian_blur_direct_auto_schedule.a \
	$(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	$< ../images/rgb.png $(BIN)/$*/out.png

clean:
	rm -rf $(BIN)
