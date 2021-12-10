#ifndef CHECK_CALL_GRAPHS_H
#define CHECK_CALL_GRAPHS_H

#include <algorithm>
#include <assert.h>
#include <functional>
#include <map>
#include <numeric>
#include <stdio.h>
#include <string.h>

#include "Halide.h"

typedef std::map<std::string, std::vector<std::string>> CallGraphs;

// For each producer node, find all functions that it calls.
class CheckCalls : public Halide::Internal::IRMutator {
public:
    CallGraphs calls;  // Caller -> vector of callees
    std::string producer = "";

private:
    using Halide::Internal::IRMutator::visit;

    Halide::Internal::Stmt visit(const Halide::Internal::ProducerConsumer *op) override {
        if (op->is_producer) {
            std::string old_producer = producer;
            producer = op->name;
            calls[producer];  // Make sure each producer is allocated a slot
            // Group the callees of the 'produce' and 'update' together
            auto new_stmt = mutate(op->body);
            producer = old_producer;
            return new_stmt;
        } else {
            return Halide::Internal::IRMutator::visit(op);
        }
    }

    Halide::Expr visit(const Halide::Internal::Load *op) override {
        if (!producer.empty()) {
            assert(calls.count(producer) > 0);
            std::vector<std::string> &callees = calls[producer];
            if (std::find(callees.begin(), callees.end(), op->name) == callees.end()) {
                callees.push_back(op->name);
            }
        }
        return Halide::Internal::IRMutator::visit(op);
    }
};

// These are declared "inline" to avoid "unused function" warnings
inline int check_call_graphs(Halide::Pipeline p, CallGraphs &expected) {
    // Add a custom lowering pass that scrapes the call graph. We give ownership
    // of it to the Pipeline, whose lifetime escapes this function.
    CheckCalls *checker = new CheckCalls;
    p.add_custom_lowering_pass(checker);
    p.compile_to_module(p.infer_arguments(), "");
    CallGraphs &result = checker->calls;

    if (result.size() != expected.size()) {
        printf("Expect %d callers instead of %d\n", (int)expected.size(), (int)result.size());
        return -1;
    }
    for (auto &iter : expected) {
        if (result.count(iter.first) == 0) {
            printf("Expect %s to be in the call graphs\n", iter.first.c_str());
            return -1;
        }
        std::vector<std::string> &expected_callees = iter.second;
        std::vector<std::string> &result_callees = result[iter.first];
        std::sort(expected_callees.begin(), expected_callees.end());
        std::sort(result_callees.begin(), result_callees.end());
        if (expected_callees != result_callees) {
            std::string expected_str = std::accumulate(
                expected_callees.begin(), expected_callees.end(), std::string{},
                [](const std::string &a, const std::string &b) {
                    return a.empty() ? b : a + ", " + b;
                });
            std::string result_str = std::accumulate(
                result_callees.begin(), result_callees.end(), std::string{},
                [](const std::string &a, const std::string &b) {
                    return a.empty() ? b : a + ", " + b;
                });

            printf("Expect callees of %s to be (%s); got (%s) instead\n",
                   iter.first.c_str(), expected_str.c_str(), result_str.c_str());
            return -1;
        }
    }
    return 0;
}

template<typename T, typename F>
inline int check_image2(const Halide::Buffer<T> &im, const F &func) {
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            T correct = func(x, y);
            if (im(x, y) != correct) {
                std::cout << "im(" << x << ", " << y << ") = " << im(x, y)
                          << " instead of " << correct << "\n";
                return -1;
            }
        }
    }
    return 0;
}

template<typename T, typename F>
inline int check_image3(const Halide::Buffer<T> &im, const F &func) {
    for (int z = 0; z < im.channels(); z++) {
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                T correct = func(x, y, z);
                if (im(x, y, z) != correct) {
                    std::cout << "im(" << x << ", " << y << ", " << z << ") = "
                              << im(x, y, z) << " instead of " << correct << "\n";
                    return -1;
                }
            }
        }
    }
    return 0;
}

template<typename T, typename F>
inline auto  // SFINAE: returns int if F has arity of 2
check_image(const Halide::Buffer<T> &im, const F &func) -> decltype(std::declval<F>()(0, 0), int()) {
    return check_image2(im, func);
}

template<typename T, typename F>
inline auto  // SFINAE: returns int if F has arity of 3
check_image(const Halide::Buffer<T> &im, const F &func) -> decltype(std::declval<F>()(0, 0, 0), int()) {
    return check_image3(im, func);
}

#endif
