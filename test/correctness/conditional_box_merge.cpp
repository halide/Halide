// Standalone Halide 22.1 compiler regression: no Blix headers, data or libraries.
#include "Halide.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace Halide;
using namespace Halide::Internal;

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

int64_t evaluate(const Expr &expr, const std::map<std::string, Expr> &values) {
    Expr result = simplify(substitute(values, expr));
    if (auto v = as_const_int(result)) return *v;
    if (auto v = as_const_uint(result)) return static_cast<int64_t>(*v);
    std::cerr << "Expression did not become constant: " << result << "\n";
    std::exit(1);
}

Expr variable(const std::string &name) {
    return Variable::make(Int(32), name);
}

void check_simple_unions() {
    Expr flag = Variable::make(Bool(), "flag");
    Expr other = Variable::make(Bool(), "other");
    for (int kind = 0; kind < 5; ++kind) {
        Box a, b;
        a.resize(1);
        b.resize(1);
        a[0] = Interval(-7, 3);
        b[0] = Interval(1, 13);
        a.used = kind == 0 ? Expr() : flag;
        b.used = kind <= 1 ? Expr() : kind == 2 ? !flag : kind == 3 ? flag : other;
        Box merged = a;
        merge_boxes(merged, b);
        for (int f = 0; f < 2; ++f) {
            for (int o = 0; o < 2; ++o) {
                std::map<std::string, Expr> values{{"flag", cast<bool>(f)}, {"other", cast<bool>(o)}};
                bool au = !a.used.defined() || evaluate(a.used, values);
                bool bu = !b.used.defined() || evaluate(b.used, values);
                bool used = !merged.used.defined() || evaluate(merged.used, values);
                require(used == (au || bu), "Wrong union used predicate");
                if (!used) continue;
                require(evaluate(merged[0].min, values) == (au ? -7 : 1), "Wrong union minimum");
                require(evaluate(merged[0].max, values) == (bu ? 13 : 3), "Wrong union maximum");
            }
        }
        Box empty;
        merge_boxes(merged, empty);
        require(merged.size() == 1, "Merging an empty box changed dimensions");
        Box copy;
        merge_boxes(copy, merged);
        require(equal(copy[0].min, merged[0].min), "Merging into an empty box changed bounds");
    }
    Box infinite, finite;
    infinite.resize(1);
    finite.resize(1);
    infinite[0] = Interval::everything();
    finite[0] = Interval(0, 9);
    merge_boxes(infinite, finite);
    require(!infinite[0].has_lower_bound() && !infinite[0].has_upper_bound(),
            "Union lost an infinite bound");
}

}  // namespace

int main(int argc, char **argv) {
    int partitions = argc == 2 ? std::atoi(argv[1]) : 12;
    if (argc > 2 || partitions < 1 || partitions > 64) {
        std::cerr << "Usage: merge_boxes_repro [partitions: 1..64]\n";
        return 2;
    }
    check_simple_unions();
    Expr geometry = Variable::make(Bool(), "geometry");
    Expr rest = const_true();
    Box result;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < partitions; ++i) {
        Expr layout = Variable::make(Bool(), "layout_" + std::to_string(i));
        Expr selected = simplify(rest && layout);
        rest = simplify(rest && !layout);
        for (int g = 0; g < 2; ++g) {
            std::string prefix = "s" + std::to_string(2 * i + g);
            Box b;
            b.resize(2);
            b[0] = Interval(variable(prefix + ".row.min"), variable(prefix + ".row.max"));
            b[1] = Interval::single_point(variable(prefix + ".piece"));
            b.used = simplify(selected && (g ? geometry : !geometry));
            // LHS indices, the predicate and RHS can request the same producer box.
            for (int repeat = 0; repeat < 3; ++repeat) {
                merge_boxes(result, b);
            }
        }
    }
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::mt19937 rng(0x221);
    for (int trial = 0; trial < 256; ++trial) {
        std::map<std::string, Expr> values;
        bool forced = trial > 0 && trial <= 2 * partitions;
        int geometry_value = forced ? (trial - 1) % 2 : rng() & 1;
        values["geometry"] = cast<bool>(geometry_value);
        int selected = -1;
        for (int i = 0; i < partitions; ++i) {
            bool on = forced ? i == (trial - 1) / 2 : trial != 0 && (rng() & 1);
            values["layout_" + std::to_string(i)] = cast<bool>(on);
            if (selected < 0 && on) selected = 2 * i + geometry_value;
        }
        for (int i = 0; i < 2 * partitions; ++i) {
            std::string prefix = "s" + std::to_string(i);
            int minimum = static_cast<int>(rng() % 101) - 50;
            values[prefix + ".row.min"] = minimum;
            values[prefix + ".row.max"] = minimum + static_cast<int>(rng() % 33);
            values[prefix + ".piece"] = static_cast<int>(rng() % 5);
        }
        bool used = !result.used.defined() || evaluate(result.used, values);
        require(used == (selected >= 0), "Wrong partition used predicate");
        if (!used) continue;
        std::string prefix = "s" + std::to_string(selected);
        require(evaluate(result[0].min, values) == evaluate(values.at(prefix + ".row.min"), {}),
                "Wrong selected row minimum");
        require(evaluate(result[0].max, values) == evaluate(values.at(prefix + ".row.max"), {}),
                "Wrong selected row maximum");
        require(evaluate(result[1].min, values) == evaluate(values.at(prefix + ".piece"), {}),
                "Wrong selected piece minimum");
        require(evaluate(result[1].max, values) == evaluate(values.at(prefix + ".piece"), {}),
                "Wrong selected piece maximum");
    }
    std::cout << "{\"partitions\":" << partitions << ",\"merge_seconds\":" << seconds
              << ",\"semantic_trials\":256}\n";
    std::cout << "Success!\n";
}
