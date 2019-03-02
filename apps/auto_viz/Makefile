include ../support/Makefile.inc

VARIANTS = \
naive_up naive_down \
lessnaive_up lessnaive_down \
complex_up complex_down

LIBRARIES = $(foreach V,$(VARIANTS),$(BIN)/auto_viz_demo_$(V).a)
OUTPUTS = $(foreach V,$(VARIANTS),$(BIN)/out_$(V).png)

TRACE_TARGET=$(HL_TARGET)-trace_all

all: $(OUTPUTS)

$(BIN)/auto_viz_demo.generator: auto_viz_demo_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/auto_viz_demo_%.a: $(BIN)/auto_viz_demo.generator
	@mkdir -p $(@D)
	$^ -g auto_viz_demo -o $(BIN) -f auto_viz_demo_$* \
	target=$(TRACE_TARGET)-no_runtime \
	schedule_type=$$(echo $* | cut -d_ -f1) \
	upsample=$$(echo $* | cut -d_ -f2 | sed 's/up/true/;s/down/false/')

$(BIN)/runtime.a: $(BIN)/auto_viz_demo.generator
	@mkdir -p $(@D)
	$^ -r runtime -o $(BIN) target=$(TRACE_TARGET)

$(BIN)/auto_viz_demo: auto_viz_demo.cpp $(LIBRARIES) $(BIN)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I $(BIN) $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

$(BIN)/out_%_up.png: $(BIN)/auto_viz_demo
	@mkdir -p $(@D)
	@echo Creating out_$*_up.png...
	@$< \
	$(IMAGES)/rgb_small.png \
	$(BIN)/out_$*_up.png \
	-s $$(echo $* | cut -d_ -f1) \
	-f 4.0

$(BIN)/out_%_down.png: $(BIN)/auto_viz_demo
	@mkdir -p $(@D)
	@echo Creating out_$*_down.png...
	@$< \
	$(IMAGES)/rgb.png \
	$(BIN)/out_$*_down.png \
	-s $$(echo $* | cut -d_ -f1) \
	-f 0.5

clean:
	rm -rf $(BIN)

test: all

../../bin/HalideTraceViz: ../../util/HalideTraceViz.cpp
	$(MAKE) -C ../../ bin/HalideTraceViz

.SECONDARY:

$(BIN)/viz_%.mp4: $(BIN)/auto_viz_demo ../support/viz_auto.sh ../../bin/HalideTraceViz
	@mkdir -p $(@D)
	HL_AVCONV=$(HL_AVCONV) bash ../support/viz_auto.sh \
		"$< $(IMAGES)/rgb_small.png /tmp/$*.png -s $* -f 0.5 " \
		"../../bin/HalideTraceViz --auto_layout" \
		$@

# make viz_naive
# make viz_lessnaive
# make viz_complex
viz_%: $(BIN)/viz_%.mp4
	$(HL_VIDEOPLAYER) $^
