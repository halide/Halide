#include <stdlib.h>
#include <string>

// schema for storing information about generated pipeline

struct DAGSchema {
    uint64_t program_id;
    std::string func_name;
    uint64_t stage_type;
    uint64_t stage_index; // topological index of this stage
    uint64_t input_index; // topological index of input
    std::string input_name; // func name 
};

struct FuncDefSchema {
    uint64_t program_id;
    std::string func_name;
    uint64_t stage_index;
    std::string def;
};
