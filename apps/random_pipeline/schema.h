#include <stdlib.h>
#include <string>

using std::to_string;

// schema for storing information about generated pipeline

struct DAGSchema {
    uint64_t program_id;
    std::string func_name;
    uint64_t stage_type;
    uint64_t stage_index; // topological index of this stage
    uint64_t input_index; // topological index of input
    std::string input_name; // func name 

		DAGSchema(uint64_t program_id, std::string func_name, 
							uint64_t stage_type, uint64_t stage_index, 
							uint64_t input_index, std::string input_name) 
							: program_id(program_id), func_name(func_name),
								stage_type(stage_type), stage_index(stage_index),
								input_index(input_index), input_name(input_name) {}

    std::string dump() {
        return to_string(program_id) + "," + func_name + "," + to_string(stage_type) 
        + to_string(stage_index) + "," + to_string(input_index) + "," + input_name;
    }
};

struct FuncDefSchema {
    uint64_t program_id;
    std::string func_name;
    uint64_t stage_index;
    std::string def;
    
		FuncDefSchema(uint64_t program_id, std::string func_name, 
									uint64_t stage_index, std::string def)
							: program_id(program_id), func_name(func_name),
								stage_index(stage_index), def(def) {}

    std::string dump() {
        return to_string(program_id) + "," + func_name + "," + to_string(stage_index) + "," + def;
    }
};
