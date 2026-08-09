#pragma once
#include "Halide.h"
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>

// Returns map (key: function name,  val: line of attributes)
std::unordered_map<std::string, std::string> parse_llvm_ir_attributes(const std::string &llvm_ir_str) {
    std::unordered_map<std::string, std::string> result;

    // attribute id (#N) -> list of functions waiting for it
    std::unordered_map<std::string, std::vector<std::string>> pending;
    std::istringstream iss(llvm_ir_str);
    std::string line;

    // define|declare ... @func(...) ... #N
    std::regex func_regex(R"(^\s*(define|declare)\b.*@([A-Za-z_.$][\w.$]*)\b.*#(\d+).*)");

    // attributes #N =
    std::regex attr_def_regex(R"(^\s*attributes\s+#\d+\s*=.*)");

    while (std::getline(iss, line)) {
        std::smatch m;

        // function definition
        if (std::regex_match(line, m, func_regex)) {
            std::string func_name = m[2].str();
            std::string attr_id = "#" + m[3].str();
            pending[attr_id].push_back(func_name);
            continue;
        }

        // attribute definition
        if (std::regex_match(line, m, attr_def_regex)) {
            std::string &attr_line = line;

            std::smatch id_match;
            if (std::regex_search(attr_line, id_match, std::regex(R"(#\d+)"))) {
                std::string attr_id = id_match[0].str();

                auto it = pending.find(attr_id);
                if (it != pending.end()) {
                    for (const auto &func : it->second) {
                        result[func] = attr_line;
                    }
                    pending.erase(it);
                }
            }
        }
    }

    return result;
}

std::unordered_map<std::string, std::string> parse_llvm_ir_attributes_from_file(const std::string &llvm_file_name) {
    Halide::Internal::assert_file_exists(llvm_file_name);
    std::ifstream llvm_ir;
    llvm_ir.open(llvm_file_name, std::ios::in);
    if (!llvm_ir.is_open()) {
        std::cerr << "Error: cannot open file: " << llvm_file_name << "\n";
        return {};
    }
    std::ostringstream buffer;
    buffer << llvm_ir.rdbuf();
    llvm_ir.close();
    return parse_llvm_ir_attributes(buffer.str());
}
