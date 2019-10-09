include ../support/Makefile.inc

all: $(BIN)/find_rules

$(BIN)/find_rules: find_rules.cpp parser.cpp parser.h z3.cpp z3.h expr_util.cpp expr_util.h $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS)

clean:
	rm -rf $(BIN)
