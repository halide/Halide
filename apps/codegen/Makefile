include ../support/Makefile.inc

OBJECTS = $(BIN)/single_rule.o $(BIN)/merge_rules.o

all: $(BIN)/single_rule_tool $(BIN)/merge_rules_tool

CXXFLAGS += -O3 -g

LDFLAGS += -L ../../distrib/bin -L ../../distrib/lib -Wl,-rpath ../../distrib/lib

STATIC_LDFLAGS = $(filter-out -lpthread,$(LDFLAGS)) -pthread -Wl,--whole-archive -lpthread -Wl,--no-whole-archive

$(BIN)/%.o: %.cpp %.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN)/single_rule_tool: single_rule_tool.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

# TODO: add $(OBJECTS) once code is cleaner / more modular
$(BIN)/merge_tool: merge_tool.cpp $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) -lHalide

# $(BIN)/merge_rules_tool: merge_rules_tool.cpp $(OBJECTS) $(LIB_HALIDE)
# 	@mkdir -p $(@D)
# 	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

clean:
	rm -rf $(BIN)
