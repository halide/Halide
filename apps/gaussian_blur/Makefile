include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/filter

$(GENERATOR_BIN)/gaussian_blur.generator: gaussian_blur_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

LIBS := $(BIN)/%/gaussian_blur_direct.a $(BIN)/%/runtime.a
HEADERS := gaussian_blur_direct.h
define GAUSSIAN_BLUR
LIBS += $(BIN)/%/gaussian_blur_$(1)_$(2)_$(3).a
HEADERS += gaussian_blur_$(1)_$(2)_$(3).h
$(BIN)/%/gaussian_blur_$(1)_$(2)_$(3).a: $(GENERATOR_BIN)/gaussian_blur.generator
	@mkdir -p $$(@D)
	$$< -g gaussian_blur -f gaussian_blur_$(1)_$(2)_$(3) -e $(GENERATOR_OUTPUTS),stmt_html -o $(BIN)/$$* target=$$*-no_runtime upsample_order=$(1) downsample_order=$(2) factor=$(3)
endef

$(foreach upsample_order,2 3 4, \
$(foreach downsample_order,1 2 3, \
$(foreach factor,2 4 8 16, \
$(eval $(call GAUSSIAN_BLUR,$(upsample_order),$(downsample_order),$(factor))))))

# combined header
$(BIN)/%/blurs.h: 
	rm -f $@
	$(foreach h,$(HEADERS),echo '#include "$(h)"' >> $@;)

$(BIN)/%/gaussian_blur_direct.a: $(GENERATOR_BIN)/gaussian_blur.generator
	@mkdir -p $(@D)
	$< -g gaussian_blur_direct -f gaussian_blur_direct -e $(GENERATOR_OUTPUTS),stmt_html -o $(BIN)/$* target=$*-no_runtime

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/gaussian_blur.generator 
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

.PRECIOUS: $(LIBS) $(BIN)/%/blurs.h

$(BIN)/%/filter: filter.cpp $(LIBS) $(BIN)/%/blurs.h
	@mkdir -p $(@D)
	$(CXX) -fopenmp $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

test: $(BIN)/$(HL_TARGET)/filter
	$< ../images/gray.png 10

clean:
	rm -rf $(BIN)
