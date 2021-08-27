#ifndef HL_MERGE_RULES_H
#define HL_MERGE_RULES_H

#include "Halide.h"
#include "rewrite_rule.h"

std::string merge_rules_function(const std::vector<RewriteRule> &rules, std::string func_name, std::string var_name);

#endif // HL_MERGE_RULES_H
