include ../support/Makefile.inc

all: $(BIN)/out_box_up.png $(BIN)/out_linear_up.png $(BIN)/out_cubic_up.png $(BIN)/out_lanczos_up.png $(BIN)/out_box_down.png $(BIN)/out_linear_down.png $(BIN)/out_cubic_down.png $(BIN)/out_lanczos_down.png

$(BIN)/resize_generator: resize_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/resize_%_up.a: $(BIN)/resize_generator
	@mkdir -p $(@D)
	$^ -g resize -o $(BIN) -f resize_$*_up target=$(HL_TARGET)-no_runtime interpolation_type=$* upsample=true

$(BIN)/resize_%_down.a: $(BIN)/resize_generator
	@mkdir -p $(@D)
	$^ -g resize -o $(BIN) -f resize_$*_down target=$(HL_TARGET)-no_runtime interpolation_type=$* upsample=false

$(BIN)/runtime.a: $(BIN)/resize_generator
	@mkdir -p $(@D)
	$^ -r runtime -o $(BIN) target=$(HL_TARGET)

$(BIN)/resize: resize.cpp $(BIN)/resize_box_up.a $(BIN)/resize_linear_up.a $(BIN)/resize_cubic_up.a $(BIN)/resize_lanczos_up.a $(BIN)/resize_box_down.a $(BIN)/resize_linear_down.a $(BIN)/resize_cubic_down.a $(BIN)/resize_lanczos_down.a $(BIN)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I $(BIN) $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

$(BIN)/out_%_up.png: $(BIN)/resize
	@mkdir -p $(@D)
	$(BIN)/resize $(IMAGES)/rgb_small.png $(BIN)/out_$*_up.png -f 4.0 -t $*

$(BIN)/out_%_down.png: $(BIN)/resize
	@mkdir -p $(@D)
	$(BIN)/resize $(IMAGES)/rgb.png $(BIN)/out_$*_down.png -f 0.5 -t $*

clean:
	rm -rf $(BIN)
