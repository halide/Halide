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
class CheckCalls : public Halide::Internal::IRVisitor {
public:
    CallGraphs calls;  // Caller -> vector of callees
    std::string producer = "";
    std::map<std::string, std::string> module_producers;
    Halide::Target target;

    // TODO(zvookin|abadams): Figure out how to get the right graph across multiple
    // lowered functions. Iterating in reverse order doesn't seem to change the result,
    // which sort of makes sense as it seems the traversal just isn't seeing the edge
    // between the callers of the newly introduced closures and the closures themselves.
    void add_module(const Halide::Module &m) {
        const auto &functions = m.functions();
        for (size_t i = functions.size(); i > 0; i--) {
            auto dominating_producer = module_producers.find(functions[i - 1].name.c_str());
            if (dominating_producer != module_producers.end()) {
                producer = dominating_producer->second;
            }
            target = m.target();
            functions[i - 1].body.accept(this);
            producer = "";
        }
    }

private:
    using Halide::Internal::IRVisitor::visit;

    void visit(const Halide::Internal::Call *op) override {
        if (op->is_intrinsic(Halide::Internal::Call::resolve_function_name)) {
            assert(op->args.size() == 1);

            const Halide::Internal::Call *decl_call = op->args[0].as<Halide::Internal::Call>();
            assert(decl_call != nullptr);
            std::string name;
            if (decl_call->call_type == Halide::Internal::Call::ExternCPlusPlus) {
                std::vector<std::string> namespaces;
                name = Halide::Internal::extract_namespaces(decl_call->name, namespaces);
                std::vector<Halide::ExternFuncArgument> mangle_args;
                for (const auto &arg : decl_call->args) {
                    mangle_args.emplace_back(arg);
                }
                name = Halide::Internal::cplusplus_function_mangled_name(name, namespaces, decl_call->type, mangle_args, target);
            } else {
                name = decl_call->name;
            }

            if (module_producers.find(name) == module_producers.end()) {
                module_producers[name] = producer;
            }
        }
        Halide::Internal::IRVisitor::visit(op);
    }

    void visit(const Halide::Internal::ProducerConsumer *op) override {
        if (op->is_producer) {
            std::string old_producer = producer;
            producer = op->name;
            calls[producer];  // Make sure each producer is allocated a slot
            // Group the callees of the 'produce' and 'update' together
            op->body.accept(this);
            producer = old_producer;
        } else {
            Halide::Internal::IRVisitor::visit(op);
        }
    }

    void visit(const Halide::Internal::Load *op) override {
        Halide::Internal::IRVisitor::visit(op);
        if (!producer.empty()) {
            assert(calls.count(producer) > 0);
            std::vector<std::string> &callees = calls[producer];
            if (std::find(callees.begin(), callees.end(), op->name) == callees.end()) {
                callees.push_back(op->name);
            }
        }
    }
};

inline void print_graph(const CallGraphs &g) {
    for (const auto &node : g) {
        printf("Graph node %s:\n", node.first.c_str());
        for (const auto &edge : node.second) {
            printf("    %s\n", edge.c_str());
        }
    }
}

// These are declared "inline" to avoid "unused function" warnings
inline int check_call_graphs(CallGraphs &result, CallGraphs &expected) {
    if (result.size() != expected.size()) {
        printf("Expected---\n");
        print_graph(expected);
        printf("Result---\n");
        print_graph(result);
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
