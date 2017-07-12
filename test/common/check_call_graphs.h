#ifndef CHECK_CALL_GRAPHS_H
#define CHECK_CALL_GRAPHS_H

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <numeric>
#include <functional>
#include <map>

#include "Halide.h"

typedef std::map<std::string, std::vector<std::string>> CallGraphs;

// For each producer node, find all functions that it calls.
class CheckCalls : public Halide::Internal::IRVisitor {
public:
    CallGraphs calls; // Caller -> vector of callees
    std::string producer = "";
private:
    using Halide::Internal::IRVisitor::visit;

    void visit(const Halide::Internal::ProducerConsumer *op) {
        if (op->is_producer) {
            std::string old_producer = producer;
            producer = op->name;
            calls[producer]; // Make sure each producer is allocated a slot
            // Group the callees of the 'produce' and 'update' together
            op->body.accept(this);
            producer = old_producer;
        } else {
            Halide::Internal::IRVisitor::visit(op);
        }
    }

    void visit(const Halide::Internal::Load *op) {
        Halide::Internal::IRVisitor::visit(op);
        if (!producer.empty()) {
            assert(calls.count(producer) > 0);
            std::vector<std::string> &callees = calls[producer];
            if(std::find(callees.begin(), callees.end(), op->name) == callees.end()) {
                callees.push_back(op->name);
            }
        }
    }
};

// These are declared "inline" to avoid "unused function" warnings
inline int check_call_graphs(CallGraphs &result, CallGraphs &expected) {
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

            printf("Expect calless of %s to be (%s); got (%s) instead\n",
                    iter.first.c_str(), expected_str.c_str(), result_str.c_str());
            return -1;
        }

    }
    return 0;
}

inline int check_image(const Halide::Buffer<int> &im, const std::function<int(int,int)> &func) {
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = func(x, y);
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    return 0;
}

inline int check_image(const Halide::Buffer<int> &im, const std::function<int(int,int,int)> &func) {
    for (int z = 0; z < im.channels(); z++) {
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = func(x, y, z);
                if (im(x, y, z) != correct) {
                    printf("im(%d, %d, %d) = %d instead of %d\n",
                           x, y, z, im(x, y, z), correct);
                    return -1;
                }
            }
        }
    }
    return 0;
}

#endif
