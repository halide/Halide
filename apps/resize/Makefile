include ../support/Makefile.inc

VARIANTS = \
box_float32_up box_float32_down \
box_uint16_up box_uint16_down \
box_uint8_up box_uint8_down \
linear_float32_up linear_float32_down \
linear_uint16_up linear_uint16_down \
linear_uint8_up linear_uint8_down \
cubic_float32_up cubic_float32_down \
cubic_uint16_up cubic_uint16_down \
cubic_uint8_up cubic_uint8_down \
lanczos_float32_up lanczos_float32_down \
lanczos_uint16_up lanczos_uint16_down \
lanczos_uint8_up lanczos_uint8_down

LIBRARIES = $(foreach V,$(VARIANTS),$(BIN)/resize_$(V).a)
OUTPUTS = $(foreach V,$(VARIANTS),$(BIN)/out_$(V).png)

all: $(OUTPUTS)

$(BIN)/resize.generator: resize_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/resize_%.a: $(BIN)/resize.generator
	@mkdir -p $(@D)
	$^ -g resize -o $(BIN) -f resize_$* \
	target=$(HL_TARGET)-no_runtime \
	interpolation_type=$$(echo $* | cut -d_ -f1) \
	input.type=$$(echo $* | cut -d_ -f2) \
	upsample=$$(echo $* | cut -d_ -f3 | sed 's/up/true/;s/down/false/')

$(BIN)/runtime.a: $(BIN)/resize.generator
	@mkdir -p $(@D)
	$^ -r runtime -o $(BIN) target=$(HL_TARGET)

$(BIN)/resize: resize.cpp $(LIBRARIES) $(BIN)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I $(BIN) $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

# Make the small input used to test upsampling with our highest-quality downsampling method
$(BIN)/rgb_small.png: $(BIN)/resize
	@mkdir -p $(@D)
	@$(BIN)/resize \
	$(IMAGES)/rgb.png \
	$(BIN)/rgb_small.png \
	-i lanczos \
	-t float32 \
	-f 0.125

$(BIN)/out_%_up.png: $(BIN)/resize $(BIN)/rgb_small.png
	@mkdir -p $(@D)
	@$(BIN)/resize \
	$(BIN)/rgb_small.png \
	$(BIN)/out_$*_up.png \
	-i $$(echo $* | cut -d_ -f1) \
	-t $$(echo $* | cut -d_ -f2) \
	-f 4.0

$(BIN)/out_%_down.png: $(BIN)/resize
	@mkdir -p $(@D)
	@$(BIN)/resize \
	$(IMAGES)/rgb.png \
	$(BIN)/out_$*_down.png \
	-i $$(echo $* | cut -d_ -f1) \
	-t $$(echo $* | cut -d_ -f2) \
	-f 0.5

clean:
	rm -rf $(BIN)


test: all


../../bin/HalideTraceViz: ../../util/HalideTraceViz.cpp
	$(MAKE) -C ../../ bin/HalideTraceViz

.SECONDARY:

$(BIN)/viz_auto_%.mp4: $(BIN)/resize ../support/viz_auto.sh ../../bin/HalideTraceViz
	@mkdir -p $(@D)
	HL_AVCONV=$(HL_AVCONV) bash ../support/viz_auto.sh \
		"$(BIN)/resize $(IMAGES)/rgb_small.png /tmp/$*.png -p 0 -b 1 -i $* -t float32 -f 0.5" \
		"../../bin/HalideTraceViz --timestep 1000" \
		$@

viz_auto_%: $(BIN)/viz_auto_%.mp4
	$(HL_VIDEOPLAYER) $^
