include ../support/Makefile.inc

VARIANTS = naive lessnaive complex

OUTPUTS = $(foreach V,$(VARIANTS),$(BIN)/$(HL_TARGET)/out_$(V)_up.png $(BIN)/$(HL_TARGET)/out_$(V)_down.png)

all: $(OUTPUTS)

$(GENERATOR_BIN)/auto_viz_demo.generator: auto_viz_demo_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# Can't have multiple wildcards in Make, so we'll use a macro
# to stamp out all the rules we need
define GEN_RULES
$$(BIN)/%/auto_viz_demo_$(1)_up.a: $$(GENERATOR_BIN)/auto_viz_demo.generator
	@mkdir -p $$(@D)
	$$^ -g auto_viz_demo -o $$(@D) -f auto_viz_demo_$(1)_up \
		target=$$*-no_runtime-trace_all \
		schedule_type=$$$$(echo $(1) | cut -d_ -f1) \
		upsample=true

$$(BIN)/%/auto_viz_demo_$(1)_down.a: $$(GENERATOR_BIN)/auto_viz_demo.generator
	@mkdir -p $$(@D)
	$$^ -g auto_viz_demo -o $$(@D) -f auto_viz_demo_$(1)_down \
		target=$$*-no_runtime-trace_all \
		schedule_type=$$$$(echo $(1) | cut -d_ -f1) \
		upsample=false

$$(BIN)/%/out_$(1)_up.png: $$(BIN)/%/auto_viz_demo
	@mkdir -p $$(@D)
	@echo Creating out_$(1)_up.png...
	@$$< \
		$$(IMAGES)/rgb_small.png \
		$$@ \
		-s $$$$(echo $(1) | cut -d_ -f1) \
		-f 4.0

$$(BIN)/%/out_$(1)_down.png: $$(BIN)/%/auto_viz_demo
	@mkdir -p $$(@D)
	@echo Creating out_$(1)_down.png...
	@$$< \
		$$(IMAGES)/rgb.png \
		$$@ \
		-s $$$$(echo $(1) | cut -d_ -f1) \
		-f 0.5

$$(BIN)/%/viz_$(1).mp4: $$(BIN)/%/auto_viz_demo ../support/viz_auto.sh ../../bin/HalideTraceViz
	@mkdir -p $$(@D)
	HL_AVCONV=$$(HL_AVCONV) bash ../support/viz_auto.sh \
		"$$< $$(IMAGES)/rgb_small.png /tmp/$(1).png -s $(1) -f 0.5 " \
		"../../bin/HalideTraceViz --auto_layout" \
		$$@
endef

$(foreach V,$(VARIANTS),$(eval $(call GEN_RULES,$(V))))

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/auto_viz_demo.generator
	@mkdir -p $(@D)
	$^ -r runtime -o $(@D) target=$*

LIBRARIES = $(foreach V,$(VARIANTS),$(BIN)/%/auto_viz_demo_$(V)_up.a $(BIN)/%/auto_viz_demo_$(V)_down.a)
$(BIN)/%/auto_viz_demo: auto_viz_demo.cpp $(BIN)/%/runtime.a $(LIBRARIES)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

clean:
	rm -rf $(BIN)

test: all

../../bin/HalideTraceViz: ../../util/HalideTraceViz.cpp
	$(MAKE) -C ../../ bin/HalideTraceViz

# make viz_naive
# make viz_lessnaive
# make viz_complex
viz_%: $(BIN)/$(HL_TARGET)/viz_%.mp4
	$(HL_VIDEOPLAYER) $^

viz: viz_naive viz_lessnaive viz_complex
