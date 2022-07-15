THIS_MAKEFILE = $(realpath $(filter %Makefile, $(MAKEFILE_LIST)))
SRC = $(strip $(shell dirname $(THIS_MAKEFILE)))
HALIDE_SRC_ROOT = $(realpath $(SRC)/../../../)
COMMON_DIR ?= $(realpath $(SRC)/../common/)

HALIDE_DISTRIB_PATH ?= $(HALIDE_SRC_ROOT)/distrib

$(info Looking for Halide distro at $(HALIDE_DISTRIB_PATH). If this is incorrect, set the make variable HALIDE_DISTRIB_PATH)

# Don't include an autoscheduler in the generator deps
AUTOSCHEDULER=
include $(HALIDE_SRC_ROOT)/apps/support/Makefile.inc

# Add the relative location of libHalide.so in the rpath in a distro
ifeq ($(UNAME), Darwin)
HALIDE_RPATH_FOR_BIN = '-Wl,-rpath,@executable_path/../lib'
HALIDE_RPATH_FOR_LIB = '-Wl,-rpath,@loader_path'
else
HALIDE_RPATH_FOR_BIN = '-Wl,-rpath,$$ORIGIN/../lib'
HALIDE_RPATH_FOR_LIB = '-Wl,-rpath,$$ORIGIN'
endif

CXXFLAGS += -I$(COMMON_DIR)

AUTOSCHED_WEIGHT_OBJECTS=$(BIN)/baseline_weights.o

$(BIN)/binary2cpp: $(HALIDE_SRC_ROOT)/tools/binary2cpp.cpp
	@mkdir -p $(@D)
	$(CXX) $< -o $@

$(BIN)/baseline_weights.cpp: $(BIN)/binary2cpp $(SRC)/baseline.weights
	@mkdir -p $(@D)
	$(BIN)/binary2cpp baseline_weights < $(SRC)/baseline.weights > $@

$(BIN)/baseline_weights.o: $(BIN)/baseline_weights.cpp
	$(CXX) -c $< -o $@

AUTOSCHED_COST_MODEL_LIBS=\
$(BIN)/cost_model/cost_model.a \
$(BIN)/cost_model/train_cost_model.a \

$(BIN)/cost_model.generator: $(SRC)/cost_model_generator.cpp \
				$(SRC)/cost_model_schedule.h \
				$(SRC)/NetworkSize.h \
				$(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(USE_EXPORT_DYNAMIC) $(LIBHALIDE_LDFLAGS)

$(BIN)/auto_schedule_runtime.a: $(BIN)/cost_model.generator
	@mkdir -p $(@D)
	$^ -r auto_schedule_runtime -o $(BIN) target=$(HL_TARGET)

$(BIN)/cost_model/%.a: $(BIN)/cost_model.generator
	@mkdir -p $(@D)
	$^ -g $* -o $(BIN)/cost_model -f $* target=$(HL_TARGET)-no_runtime -e stmt,static_library,h,assembly

# It's important to use dynamic lookups for undefined symbols here: all of libHalide
# is expected to be present (in the loading binary), so we explicitly make the symbols
# undefined rather than dependent on libHalide.so.
$(BIN)/libautoschedule_adams2019.$(SHARED_EXT): $(SRC)/AutoSchedule.cpp \
				$(SRC)/ASLog.cpp \
				$(SRC)/Cache.h \
				$(SRC)/Cache.cpp \
				$(SRC)/DefaultCostModel.h \
				$(SRC)/DefaultCostModel.cpp \
				$(SRC)/Weights.h \
				$(SRC)/Weights.cpp \
				$(SRC)/FunctionDAG.h \
				$(SRC)/FunctionDAG.cpp \
				$(SRC)/LoopNest.h \
				$(SRC)/LoopNest.cpp \
				$(SRC)/Featurization.h \
				$(SRC)/CostModel.h \
				$(SRC)/State.h \
				$(SRC)/State.cpp \
				$(SRC)/Timer.h \
				$(SRC)/PerfectHashMap.h \
				$(AUTOSCHED_WEIGHT_OBJECTS) \
				$(AUTOSCHED_COST_MODEL_LIBS) \
				$(GENERATOR_DEPS) \
				$(BIN)/auto_schedule_runtime.a
	@mkdir -p $(@D)
	$(CXX) -shared $(USE_EXPORT_DYNAMIC) -fPIC -fvisibility=hidden -fvisibility-inlines-hidden $(CXXFLAGS) $(OPTIMIZE) -I $(BIN)/cost_model $(filter-out %.h $(LIBHALIDE_LDFLAGS),$^) -o $@ $(HALIDE_SYSTEM_LIBS) $(HALIDE_RPATH_FOR_LIB)

$(BIN)/retrain_cost_model: $(SRC)/retrain_cost_model.cpp \
				$(SRC)/ASLog.cpp \
				$(SRC)/DefaultCostModel.h \
				$(SRC)/DefaultCostModel.cpp \
				$(SRC)/Weights.h \
				$(SRC)/Weights.cpp \
				$(SRC)/CostModel.h \
				$(SRC)/NetworkSize.h \
				$(AUTOSCHED_COST_MODEL_LIBS) \
				$(AUTOSCHED_WEIGHT_OBJECTS) \
				$(BIN)/auto_schedule_runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -frtti -Wall -I ../support -I $(BIN)/cost_model $(OPTIMIZE) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(USE_OPEN_MP) $(HALIDE_RPATH_FOR_BIN)

$(BIN)/featurization_to_sample: $(SRC)/featurization_to_sample.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OPTIMIZE) -o $@ 

$(BIN)/get_host_target: $(SRC)/get_host_target.cpp $(LIB_HALIDE) $(HALIDE_DISTRIB_PATH)/include/Halide.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) $(LIBHALIDE_LDFLAGS) $(OPTIMIZE) -o $@ $(HALIDE_RPATH_FOR_BIN)
$(BIN)/weightsdir_to_weightsfile: $(SRC)/weightsdir_to_weightsfile.cpp $(SRC)/Weights.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ $(OPTIMIZE) -o $@

# A sample generator to autoschedule. Note that if it statically links
# to libHalide, then it must be build with $(USE_EXPORT_DYNAMIC), or the
# autoscheduler can't find the libHalide symbols that it needs.
$(GENERATOR_BIN)/demo.generator: $(SRC)/demo_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

# To use the autoscheduler, set a few environment variables and use the -p flag to the generator to load the autoscheduler as a plugin
$(BIN)/%/demo.a: $(GENERATOR_BIN)/demo.generator $(BIN)/libautoschedule_adams2019.$(SHARED_EXT)
	@mkdir -p $(@D)
	HL_WEIGHTS_DIR=$(SRC)/baseline.weights \
	$(GENERATOR_BIN)/demo.generator -g demo -o $(@D) -f demo target=$* autoscheduler=Adams2019 autoscheduler.parallelism=32 -p $(BIN)/libautoschedule_adams2019.$(SHARED_EXT)

$(BIN)/%/demo.rungen: $(BIN)/%/RunGenMain.o $(BIN)/%/demo.registration.cpp $(BIN)/%/demo.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* $^ -o $@ $(HALIDE_SYSTEM_LIBS) $(IMAGE_IO_FLAGS)

# demonstrates single-shot use of the autoscheduler
demo: $(BIN)/$(HL_TARGET)/demo.rungen $(BIN)/libautoschedule_adams2019.$(SHARED_EXT)
	$< --benchmarks=all --benchmark_min_time=1 --estimate_all

# demonstrates an autotuning loop
# (using $(BIN) and $(SRC) here seems overkill, but makes copy-n-paste elsewhere easier)
autotune: $(GENERATOR_BIN)/demo.generator $(BIN)/featurization_to_sample $(BIN)/get_host_target $(BIN)/retrain_cost_model $(BIN)/libautoschedule_adams2019.$(SHARED_EXT) $(SRC)/autotune_loop.sh
	@mkdir -p $(@D)
	bash $(SRC)/autotune_loop.sh \
		$(GENERATOR_BIN)/demo.generator \
		demo \
		"" \
		$(SRC)/baseline.weights \
		$(BIN) \
		$(HALIDE_DISTRIB_PATH) \
		$(BIN)/samples

$(BIN)/test_perfect_hash_map: $(SRC)/test_perfect_hash_map.cpp $(SRC)/PerfectHashMap.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< -o $@

$(BIN)/test_function_dag: $(SRC)/test_function_dag.cpp $(SRC)/FunctionDAG.h $(SRC)/FunctionDAG.cpp $(SRC)/ASLog.h $(SRC)/ASLog.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# Simple jit-based test
$(BIN)/%/test: $(SRC)/test.cpp $(BIN)/libautoschedule_adams2019.$(SHARED_EXT)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) $^ -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS)

test_perfect_hash_map: $(BIN)/test_perfect_hash_map
	$^

test_function_dag: $(BIN)/test_function_dag
	$^

run_test: $(BIN)/$(HL_TARGET)/test
	HL_WEIGHTS_DIR=$(SRC)/baseline.weights LD_LIBRARY_PATH=$(BIN):$(LD_LIBRARY_PATH) $< $(BIN)/libautoschedule_adams2019.$(SHARED_EXT)

.PHONY: test clean

# Note that 'make build' and 'make test' is used by Halide buildbots
# to spot-check changes, so it's important to try a little of each of
# the important paths here, including single-shot and autotune-loop
build: $(BIN)/$(HL_TARGET)/test \
	$(BIN)/test_perfect_hash_map \
	$(BIN)/test_function_dag \
	$(BIN)/$(HL_TARGET)/included_schedule_file.rungen \
	$(GENERATOR_BIN)/demo.generator \
	$(BIN)/featurization_to_sample \
	$(BIN)/get_host_target \
	$(BIN)/retrain_cost_model \
	$(BIN)/libautoschedule_adams2019.$(SHARED_EXT)

test: run_test test_perfect_hash_map test_function_dag demo test_included_schedule_file autotune

clean:
	rm -rf $(BIN)

# A sample generator to demonstrate including autogenerated .sample.h
# files for scheduling purposes; the catch here is that we'll need
# to be able to compile the Generator two different ways:
#
#   - one that will be used to generate the .schedule.h
#   - one that will consume the .schedule.h generated above
#
# We'll use the preprocessor (GENERATING_SCHEDULE) to distinguish between these two.

$(GENERATOR_BIN)/included_schedule_file_none.generator: $(SRC)/included_schedule_file_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -DGENERATING_SCHEDULE -g $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# This is the target you build to (re)generate the schedule file.
# (Note that we only need the schedule output, so we pass `-e schedule` to
# the Generator so that it can skip producing other outputs.)
$(BIN)/%/included_schedule_file.schedule.h: $(GENERATOR_BIN)/included_schedule_file_none.generator $(BIN)/libautoschedule_adams2019.$(SHARED_EXT)
	@mkdir -p $(@D)
	HL_WEIGHTS_DIR=$(SRC)/baseline.weights \
	$< -g included_schedule_file -o $(@D) -f included_schedule_file target=$* autoscheduler=Adams2019 autoscheduler.parallelism=32 -p $(BIN)/libautoschedule_adams2019.$(SHARED_EXT) -e schedule

# Note that this depends on included_schedule_file.schedule.h rather than $(BIN)/%/included_schedule_file.schedule.h --
# the former should be generated by something like
#
#    make bin/host/included_schedule_file.schedule.h
#    cp bin/host/included_schedule_file.schedule.h included_schedule_file.schedule.h
#
$(GENERATOR_BIN)/included_schedule_file.generator: $(SRC)/included_schedule_file_generator.cpp $(SRC)/included_schedule_file.schedule.h $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -g $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# Note that this does not depend on libauto_schedule nor does it call
# the autoscheduler at build time; it includes the generated schedule (included_schedule_file.schedule.h),
# which has been added to our local source control.
$(BIN)/%/included_schedule_file.a: $(GENERATOR_BIN)/included_schedule_file.generator
	@mkdir -p $(@D)
	$< -g included_schedule_file -o $(@D) -f included_schedule_file target=$*

$(BIN)/%/included_schedule_file.rungen: $(BIN)/%/RunGenMain.o $(BIN)/%/included_schedule_file.registration.cpp $(BIN)/%/included_schedule_file.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* $^ -o $@ $(HALIDE_SYSTEM_LIBS) $(IMAGE_IO_FLAGS)

test_included_schedule_file: $(BIN)/$(HL_TARGET)/included_schedule_file.rungen
	$^ --benchmarks=all --benchmark_min_time=1 --estimate_all
