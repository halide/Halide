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

LIBRARIES = $(foreach V,$(VARIANTS),$(BIN)/%/resize_$(V).a)
OUTPUTS = $(foreach V,$(VARIANTS),$(BIN)/$(HL_TARGET)/out_$(V).png)

.PHONY: build clean test

build: $(BIN)/$(HL_TARGET)/resize

test: $(OUTPUTS)

$(GENERATOR_BIN)/resize.generator: resize_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

# Can't have multiple wildcards in Make, so we'll use a macro
# to stamp out all the rules we need
define GEN_RULE
$$(BIN)/%/resize_$(1).a: $$(GENERATOR_BIN)/resize.generator
	@mkdir -p $$(@D)
	$$^ -g resize -o $$(@D) -f resize_$(1) \
	-e $(GENERATOR_OUTPUTS) \
	target=$$*-no_runtime \
	interpolation_type=$$$$(echo $(1) | cut -d_ -f1) \
	input.type=$$$$(echo $(1) | cut -d_ -f2) \
	upsample=$$$$(echo $(1) | cut -d_ -f3 | sed 's/up/true/;s/down/false/')
endef

$(foreach V,$(VARIANTS),$(eval $(call GEN_RULE,$(V))))

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/resize.generator
	@mkdir -p $(@D)
	$^ -r runtime -o $(@D) target=$*

$(BIN)/%/resize: resize.cpp $(LIBRARIES) $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I $(BIN)/$* $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

# Make the small input used to test upsampling with our highest-quality downsampling method
$(BIN)/%/rgb_small.png: $(BIN)/%/resize
	@mkdir -p $(@D)
	@$(BIN)/$(HL_TARGET)/resize \
	$(IMAGES)/rgb.png \
	$(BIN)/$(HL_TARGET)/rgb_small.png \
	-i lanczos \
	-t float32 \
	-f 0.125

$(BIN)/$(HL_TARGET)/out_%_up.png: $(BIN)/$(HL_TARGET)/resize $(BIN)/$(HL_TARGET)/rgb_small.png
	@mkdir -p $(@D)
	@$(BIN)/$(HL_TARGET)/resize \
	$(BIN)/$(HL_TARGET)/rgb_small.png \
	$(BIN)/$(HL_TARGET)/out_$*_up.png \
	-i $$(echo $* | cut -d_ -f1) \
	-t $$(echo $* | cut -d_ -f2) \
	-f 4.0

$(BIN)/$(HL_TARGET)/out_%_down.png: $(BIN)/$(HL_TARGET)/resize
	@mkdir -p $(@D)
	@$(BIN)/$(HL_TARGET)/resize \
	$(IMAGES)/rgb.png \
	$(BIN)/$(HL_TARGET)/out_$*_down.png \
	-i $$(echo $* | cut -d_ -f1) \
	-t $$(echo $* | cut -d_ -f2) \
	-f 0.5

clean:
	rm -rf $(BIN)

../../bin/HalideTraceViz: ../../util/HalideTraceViz.cpp
	$(MAKE) -C ../../ bin/HalideTraceViz

.SECONDARY:

$(BIN)/$(HL_TARGET)/viz_auto_%.mp4: $(BIN)/$(HL_TARGET)-trace_all/resize ../support/viz_auto.sh ../../bin/HalideTraceViz
	@mkdir -p $(@D)
	HL_AVCONV=$(HL_AVCONV) bash ../support/viz_auto.sh \
		"$(BIN)/$(HL_TARGET)-trace_all/resize $(IMAGES)/rgb_small.png /tmp/$*.png -p 0 -b 1 -i $* -t float32 -f 0.5" \
		"../../bin/HalideTraceViz --timestep 1000" \
		$@

viz_auto_%: $(BIN)/$(HL_TARGET)/viz_auto_%.mp4
	$(HL_VIDEOPLAYER) $^
