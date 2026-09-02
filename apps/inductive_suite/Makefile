# Steven Raphael's standardized inductive-Func benchmarks (from the
# paper-benchmarks-rebase branch), built as JIT binaries against the
# distrib. Each prints one hb::print_row line per variant; the suite
# driver (../inductive_benchmarks.py) parses them.
#
# The external baselines need headers this box may lack: Boost.odeint
# (header-only) for the ODE app and oneTBB for prefixsum. They are
# built when present (apt: libboost-dev libtbb-dev).
HL_TARGET ?= host

include ../support/Makefile.inc

CXXFLAGS_JIT = -O3 -march=native -std=c++17 -I$(HALIDE_DISTRIB_PATH)/include -I$(HALIDE_DISTRIB_PATH)/tools
LDFLAGS_JIT = -L$(HALIDE_DISTRIB_PATH)/lib -lHalide -lpthread -ldl -Wl,-rpath,$(HALIDE_DISTRIB_PATH)/lib

BINS = $(BIN)/viterbi_log $(BIN)/chebyshev_test $(BIN)/prefixsum_bench $(BIN)/prefixsum_bench_rdom $(BIN)/prefixsum_bench_fold
ifneq (,$(wildcard /usr/include/boost/numeric/odeint.hpp))
BINS += $(BIN)/ode_observer_sparse_fused_test
endif
ifneq (,$(wildcard /usr/include/oneapi/tbb.h /usr/include/tbb/tbb.h))
BINS += $(BIN)/prefixsum_bench_tbb
endif

all: $(BINS)

$(BIN)/prefixsum_bench_tbb: prefixsum_bench_tbb.cpp ../support/bench_harness.h
	@mkdir -p $(@D)
	$(CXX) -O3 -march=native -std=c++17 $< -ltbb -lpthread -o $@

$(BIN)/%: %.cpp ../support/bench_harness.h ../support/mem_probe.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS_JIT) $< $(LDFLAGS_JIT) -o $@

clean:
	rm -rf $(BIN)
