#ifndef LOOP_NEST_PARSER_H
#define LOOP_NEST_PARSER_H

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "ASLog.h"
#include "Util.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

class LoopNestParser {
    void parse(const std::vector<std::string>& loop_nest) {
        std::unordered_map<std::string, std::vector<std::string>> stage_to_loop_nest;
        for (const auto& line : loop_nest) {
            if (line.empty()) {
                continue;
            }

            std::istringstream iss(line);
            std::vector<std::string> tokens{
                std::istream_iterator<std::string>(iss),
                std::istream_iterator<std::string>()
            };

            std::string stage = tokens.at(0);
            bool is_inlined = tokens.at(0) == "inlined:";
            if (tokens.at(0) == "realize:" || is_inlined) {
                stage = tokens.at(1);
            }

            if (stage == "gpu_none") {
                continue;
            }

            all_stages.insert(stage);

            if (is_inlined) {
                inlined.insert(stage);
                continue;
            }

            if (tokens.back() == "gpu_none") {
                partially_scheduled.insert(stage);
            }

            if (line.at(0) != ' ' && compute_root_stages.count(stage) == 0) {
                compute_root_stages[stage] = -1;
            }

            if (tokens.back() == "gpu_simd" && compute_root_stages.count(stage) == 1 && compute_root_stages[stage] == -1) {
                std::string vector_dim = tokens[tokens.size() - 3];
                compute_root_stages[stage] = std::stoi(vector_dim.substr(0, vector_dim.size() - 1));
            }

            if (partially_scheduled.count(stage) == 0) {
                stage_to_loop_nest[stage].push_back(line);
            }
        }

        for (const auto& entry : stage_to_loop_nest) {
            std::string loop_nest = "";
            for (const auto& line : entry.second) {
                loop_nest += line + "\n";
            }

            per_stage_loop_nests[entry.first] = loop_nest;
        }
    }

    std::vector<std::string> loop_nest;
    std::unordered_map<std::string, std::string> per_stage_loop_nests;
    std::unordered_set<std::string> inlined;
    std::unordered_set<std::string> partially_scheduled;
    std::unordered_map<std::string, int> compute_root_stages;
    std::unordered_set<std::string> all_stages;

public:
    LoopNestParser(const std::vector<std::string>& loop_nest)
        : loop_nest{loop_nest}
    {
        parse(loop_nest);
    }

    void dump() const {
        aslog(0) << "Partially scheduled stages:\n";
        for (const auto& s : partially_scheduled) {
            aslog(0) << s << ": " << compute_root_stages.at(s) << "\n";
        }

        aslog(0) << "\nInlined stages:\n";
        for (const auto& s : inlined) {
            aslog(0) << s << "\n";
        }

        aslog(0) << "\nFull loop nest:\n";
        for (const auto& s : loop_nest) {
            aslog(0) << s << "\n";
        }
        aslog(0) << "\n";
    }

    bool contains_sub_loop_nest(const LoopNestParser& other) const {
        for (const auto& stage : other.all_stages) {
            if (all_stages.count(stage) == 0) {
                return false;
            }

            if (other.partially_scheduled.count(stage) == 1) {
                if (compute_root_stages.count(stage) == 0) {
                    return false;
                }

                return other.compute_root_stages.at(stage) == compute_root_stages.at(stage);
            }

            if (other.inlined.count(stage) == 1 && inlined.count(stage) == 0) {
                return false;
            }

            if (other.per_stage_loop_nests.at(stage) != per_stage_loop_nests.at(stage)) {
                return false;
            }
        }

        return true;
    }

    static LoopNestParser from_string(const std::string& str) {
        std::istringstream in(str);
        std::string line;
        std::vector<std::string> loop_nest;

        while (std::getline(in, line)) {
            loop_nest.push_back(line);
        }

        return LoopNestParser(loop_nest);
    }

    static std::unique_ptr<LoopNestParser> from_file(const std::string& filename) {
        std::ifstream file(filename);
        std::string line;
        std::vector<std::string> loop_nest;

        while (std::getline(file, line)) {
            loop_nest.push_back(line);
        }

        return make_unique<LoopNestParser>(loop_nest);
    }
};

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif
