#ifndef HL_SINGLE_RULE_H
#define HL_SINGLE_RULE_H

#include "Halide.h"
#include "rewrite_rule.h"

#include <string>
#include <vector>

std::string construct_simplifier_function(const std::vector<RewriteRule> &rules, std::string func_name, std::string var_name);

#endif // HL_SINGLE_RULE_H
