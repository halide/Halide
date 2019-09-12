AUTOSCHED_SRC ?= ../autoscheduler
AUTOSCHED_BIN ?= $(AUTOSCHED_SRC)/bin

AUTOSCHED_WEIGHT_OBJECTS=$(AUTOSCHED_BIN)/baseline_weights.o

# TODO(srj): depending on something not in the distrib folder isn't strictly
# kosher, but this is still experimental
$(AUTOSCHED_BIN)/binary2cpp: ../../tools/binary2cpp.cpp
	@mkdir -p $(@D)
	$(CXX) $< -o $@

$(AUTOSCHED_BIN)/baseline_weights.cpp: $(AUTOSCHED_BIN)/binary2cpp $(AUTOSCHED_SRC)/baseline.weights
	@mkdir -p $(@D)
	$(AUTOSCHED_BIN)/binary2cpp baseline_weights < $(AUTOSCHED_SRC)/baseline.weights > $@

$(AUTOSCHED_BIN)/baseline_weights.o: $(AUTOSCHED_BIN)/baseline_weights.cpp
	$(CXX) -c $< -o $@

AUTOSCHED_COST_MODEL_LIBS=\
$(AUTOSCHED_BIN)/cost_model/cost_model.a \
$(AUTOSCHED_BIN)/cost_model/train_cost_model.a \

$(AUTOSCHED_BIN)/cost_model.generator: $(AUTOSCHED_SRC)/cost_model_generator.cpp \
							$(AUTOSCHED_SRC)/cost_model_schedule.h \
							$(AUTOSCHED_SRC)/NetworkSize.h \
							$(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(AUTOSCHED_BIN)/auto_schedule_runtime.a: $(AUTOSCHED_BIN)/cost_model.generator
	@mkdir -p $(@D)
	$^ -r auto_schedule_runtime -o $(AUTOSCHED_BIN) target=$(HL_TARGET)

$(AUTOSCHED_BIN)/cost_model/%.a: $(AUTOSCHED_BIN)/cost_model.generator
	@mkdir -p $(@D)
	$^ -g $* -o $(AUTOSCHED_BIN)/cost_model -f $* target=$(HL_TARGET)-no_runtime auto_schedule=false -e stmt,static_library,h,assembly

# It's important to use dynamic lookups for undefined symbols here: all of libHalide
# is expected to be present (in the loading binary), so we explicitly make the symbols
# undefined rather than dependent on libHalide.so.
$(AUTOSCHED_BIN)/libauto_schedule.so: $(AUTOSCHED_SRC)/AutoSchedule.cpp \
			  							$(AUTOSCHED_SRC)/ASLog.cpp \
										$(AUTOSCHED_SRC)/DefaultCostModel.h \
										$(AUTOSCHED_SRC)/DefaultCostModel.cpp \
										$(AUTOSCHED_SRC)/Weights.h \
										$(AUTOSCHED_SRC)/Weights.cpp \
										$(AUTOSCHED_SRC)/FunctionDAG.h \
										$(AUTOSCHED_SRC)/FunctionDAG.cpp \
										$(AUTOSCHED_SRC)/Featurization.h \
										$(AUTOSCHED_SRC)/CostModel.h \
										$(AUTOSCHED_SRC)/PerfectHashMap.h \
										$(AUTOSCHED_WEIGHT_OBJECTS) \
										$(AUTOSCHED_COST_MODEL_LIBS) \
										$(GENERATOR_DEPS) \
										$(AUTOSCHED_BIN)/auto_schedule_runtime.a
	@mkdir -p $(@D)
	$(CXX) -shared $(USE_EXPORT_DYNAMIC) -fPIC $(CXXFLAGS) -g $(OPTIMIZE) -I $(AUTOSCHED_BIN)/cost_model $(filter-out %.h,$^) -o $@ $(HALIDE_SYSTEM_LIBS)

$(AUTOSCHED_BIN)/retrain_cost_model: $(AUTOSCHED_SRC)/retrain_cost_model.cpp \
									$(AUTOSCHED_SRC)/ASLog.cpp \
									$(AUTOSCHED_SRC)/DefaultCostModel.h \
									$(AUTOSCHED_SRC)/DefaultCostModel.cpp \
									$(AUTOSCHED_SRC)/Weights.h \
									$(AUTOSCHED_SRC)/Weights.cpp \
									$(AUTOSCHED_SRC)/CostModel.h \
									$(AUTOSCHED_SRC)/NetworkSize.h \
									$(AUTOSCHED_COST_MODEL_LIBS) \
									$(AUTOSCHED_WEIGHT_OBJECTS) \
									$(AUTOSCHED_BIN)/auto_schedule_runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Wall -I ../support -I $(AUTOSCHED_BIN)/cost_model $(OPTIMIZE) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(USE_OPEN_MP)

$(AUTOSCHED_BIN)/featurization_to_sample: $(AUTOSCHED_SRC)/featurization_to_sample.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OPTIMIZE) -o $@

$(AUTOSCHED_BIN)/get_host_target: $(AUTOSCHED_SRC)/get_host_target.cpp $(LIB_HALIDE) $(HALIDE_DISTRIB_PATH)/include/Halide.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) $(OPTIMIZE) -o $@

$(AUTOSCHED_BIN)/weightsdir_to_weightsfile: $(AUTOSCHED_SRC)/weightsdir_to_weightsfile.cpp $(AUTOSCHED_SRC)/Weights.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ $(OPTIMIZE) -o $@

# This is the value that machine_params defaults to if no custom value is specified;
# see MachineParams::generic()
HL_MACHINE_PARAMS ?= 32,25165824,160


