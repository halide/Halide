#include <fstream>

#include "Halide.h"
#include "parser.h"

using namespace Halide;
using namespace Halide::Internal;

// Lifted from CompilerLogger.cpp
class ObfuscateNames : public IRMutator {
    using IRMutator::visit;

    std::map<std::string, std::string> remapping;

    Expr visit(const Call *op) override {
        std::vector<Expr> args;
        for (const Expr &e : op->args) {
            args.emplace_back(mutate(e));
        }

        std::string name = op->name;
        if (op->call_type == Call::Extern ||
            op->call_type == Call::ExternCPlusPlus ||
            op->call_type == Call::Halide ||
            op->call_type == Call::Image) {
            name = remap(op->name);
        }
        return Call::make(op->type, name, args, op->call_type, op->func,
                          op->value_index, op->image, op->param);
    }

    Expr visit(const Let *op) override {
        std::string name = remap(op->name);
        Expr value = mutate(op->value);
        Expr body = mutate(op->body);
        return Let::make(name, std::move(value), std::move(body));
    }

    Expr visit(const Load *op) override {
        std::string name = remap(op->name);
        Expr index = mutate(op->index);
        Expr predicate = mutate(op->predicate);
        return Load::make(op->type, name, index, op->image, op->param,
                          predicate, op->alignment);
    }

    Expr visit(const Variable *op) override {
        std::string name = remap(op->name);
        return Variable::make(op->type, name, op->image, op->param, op->reduction_domain);
    }

public:
    ObfuscateNames() = default;
    ObfuscateNames(const std::initializer_list<std::pair<const std::string, std::string>> &values)
        : remapping(values) {
    }

    std::string remap(const std::string &var_name) {
        std::string anon_name = "v" + std::to_string(remapping.size());
        auto p = remapping.insert({var_name, std::move(anon_name)});
        return p.first->second;
    }
};

// Anonymize, simplify, and dedup a file full of exprs
int main(int argc, char **argv) {
    if (argc < 3) {
        std::cout << "Usage: ./anonymize_exprs input_exprs.txt output_exprs.txt\n";
        return 0;
    }

    const std::string input_exprs_path = argv[1];
    const std::string output_exprs_path = argv[2];

    std::vector<Expr> exprs = parse_halide_exprs_from_file(input_exprs_path);
    std::set<Expr, IRDeepCompare> deduped;
    for (Expr e : exprs) {
        deduped.insert(simplify(ObfuscateNames().mutate(e)));
    }

    std::ofstream of;
    of.open(output_exprs_path);
    if (of.fail()) {
        debug(0) << "Unable to open output: " << output_exprs_path;
        assert(false);
    }
    for (Expr e : deduped) {
        of << e << "\n";
    }
    of.close();

    return 0;
}
