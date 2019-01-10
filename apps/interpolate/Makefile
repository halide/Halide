include ../support/Makefile.inc

CXXFLAGS += -g -Wall

.PHONY: clean

$(BIN)/interpolate: interpolate.cpp ../autoscheduler/SimpleAutoSchedule.h ../autoscheduler/SimpleAutoSchedule.cpp ../autoscheduler/DerivativeUtils.h ../autoscheduler/DerivativeUtils.cpp 
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) interpolate.cpp ../autoscheduler/SimpleAutoSchedule.cpp ../autoscheduler/DerivativeUtils.cpp $(LIB_HALIDE) -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/out.png: $(BIN)/interpolate
	@mkdir -p $(@D)
	$^ $(IMAGES)/rgba.png $@

clean:
	rm -rf $(BIN)

test: $(BIN)/out.png
