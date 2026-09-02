HL_TARGET ?= host

include ../support/Makefile.inc

# JIT app: builds against libHalide directly.
$(BIN)/ar_ll: ar_ll.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -O3 -march=native -ffast-math -fopenmp $< -o $@ $(LIBHALIDE_LDFLAGS) -lpthread -ldl

test: $(BIN)/ar_ll
	HL_NUM_THREADS=1 $<

clean:
	rm -rf $(BIN)
