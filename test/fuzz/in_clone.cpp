#include "Halide.h"

#include "fuzz_helpers.h"
#include <set>
#include <vector>

// Fuzz test for Func::in and Func::clone_in. We start from a simple randomly
// generated pipeline and apply a random sequence of in()/clone_in() calls,
// including clones of clones of clones and "indirect" wraps where the consumer
// passed is not a direct caller of the wrapped Func. in()/clone_in() rewrite
// the call graph eagerly and are all semantics-preserving, so the result of
// realizing the pipeline must not change no matter what we do to it. This
// stresses the wrapper/clone machinery (in particular deep-copying Funcs that
// already carry wrappers) and checks both that nothing triggers an internal
// error and that the numerics are preserved.

namespace {

using namespace Halide;

// A data-only description of a simple base pipeline, so we can build it twice
// (once as a reference, once as the pipeline we mutate) from the same random
// choices.
struct NodeDesc {
    // op == LEAF: value = ca*x + cb*y + cc.
    // otherwise: a binary/unary combination of earlier nodes a and b.
    enum Op { LEAF,
              ADD,
              SUB,
              ADD_CONST,
              SCALE_ADD } op;
    int a = 0, b = 0;   // indices of input nodes (< this node's index)
    int ca = 0, cb = 0, cc = 0, k = 0;
};

struct PipelineDesc {
    std::vector<NodeDesc> nodes;
    int output = 0;
};

PipelineDesc generate_pipeline(FuzzingContext &fuzz) {
    PipelineDesc desc;
    int n = fuzz.ConsumeIntegralInRange<int>(4, 8);
    desc.nodes.resize(n);

    // Node 0 is the only leaf; every other node combines two earlier nodes,
    // which gives a DAG rooted at the last node with plenty of shared
    // sub-Funcs (so a Func can have several direct callers).
    NodeDesc &leaf = desc.nodes[0];
    leaf.op = NodeDesc::LEAF;
    leaf.ca = fuzz.ConsumeIntegralInRange<int>(0, 3);
    leaf.cb = fuzz.ConsumeIntegralInRange<int>(0, 3);
    leaf.cc = fuzz.ConsumeIntegralInRange<int>(-4, 4);

    for (int i = 1; i < n; i++) {
        NodeDesc &node = desc.nodes[i];
        node.op = (NodeDesc::Op)fuzz.ConsumeIntegralInRange<int>(NodeDesc::ADD, NodeDesc::SCALE_ADD);
        node.a = fuzz.ConsumeIntegralInRange<int>(0, i - 1);
        node.b = fuzz.ConsumeIntegralInRange<int>(0, i - 1);
        node.k = fuzz.ConsumeIntegralInRange<int>(-4, 4);
    }
    desc.output = n - 1;
    return desc;
}

// Build the Funcs described by 'desc' into 'funcs'.
void build_funcs(const PipelineDesc &desc, Var x, Var y, std::vector<Func> &funcs) {
    funcs.resize(desc.nodes.size());
    for (size_t i = 0; i < desc.nodes.size(); i++) {
        const NodeDesc &node = desc.nodes[i];
        Func &f = funcs[i];
        switch (node.op) {
        case NodeDesc::LEAF:
            f(x, y) = node.ca * x + node.cb * y + node.cc;
            break;
        case NodeDesc::ADD:
            f(x, y) = funcs[node.a](x, y) + funcs[node.b](x, y);
            break;
        case NodeDesc::SUB:
            f(x, y) = funcs[node.a](x, y) - funcs[node.b](x, y);
            break;
        case NodeDesc::ADD_CONST:
            f(x, y) = funcs[node.a](x, y) + node.k;
            break;
        case NodeDesc::SCALE_ADD:
            f(x, y) = 2 * funcs[node.a](x, y) + funcs[node.b](x, y);
            break;
        }
    }
}

// The direct callees (among tracked Funcs) of each node's definition.
std::set<int> direct_callees(const NodeDesc &node) {
    switch (node.op) {
    case NodeDesc::LEAF:
        return {};
    case NodeDesc::ADD_CONST:
        return {node.a};
    default:
        return {node.a, node.b};
    }
}

// A model of the call graph that mirrors what in()/clone_in() do, so we can
// generate only valid operations. Because the rewrite is eager, this call graph
// stays exactly in sync with the real pipeline.
struct CallGraphModel {
    std::vector<std::set<int>> callees;  // what each Func currently calls
    // For each Func, the custom (non-global) wrappers of it that exist, and the
    // consumers already wrapped for it. Reusing a consumer, or wrapping a set of
    // consumers that mixes wrapped and unwrapped ones, is what raises user
    // errors, so we track these to avoid it.
    std::vector<std::set<int>> custom_wrappers;
    std::vector<std::set<int>> wrapped_consumers;

    int size() const {
        return (int)callees.size();
    }

    int add(std::set<int> node_callees) {
        int idx = size();
        callees.push_back(std::move(node_callees));
        custom_wrappers.emplace_back();
        wrapped_consumers.emplace_back();
        return idx;
    }

    // Resolution stops at the target or any of its custom wrappers, mirroring
    // Func.cpp's resolve_transitive_callers.
    bool is_stop(int target, int node) const {
        return node == target || custom_wrappers[target].count(node);
    }

    void collect(int target, int start, std::set<int> &visited, std::set<int> &result) const {
        if (is_stop(target, start) || !visited.insert(start).second) {
            return;
        }
        for (int c : callees[start]) {
            if (is_stop(target, c)) {
                result.insert(start);
                return;
            }
        }
        for (int c : callees[start]) {
            collect(target, c, visited, result);
        }
    }

    // The direct callers of 'target' a wrap with consumer 'consumer' would
    // rewrite. Empty if 'consumer' has no path to 'target'.
    std::set<int> resolve(int target, int consumer) const {
        std::set<int> visited, result;
        collect(target, consumer, visited, result);
        return result;
    }
};

int compare(const Buffer<int> &ref, const Buffer<int> &got) {
    for (int y = 0; y < ref.height(); y++) {
        for (int x = 0; x < ref.width(); x++) {
            if (ref(x, y) != got(x, y)) {
                std::cerr << "Mismatch at (" << x << ", " << y << "): "
                          << "expected " << ref(x, y) << ", got " << got(x, y) << "\n";
                return 1;
            }
        }
    }
    return 0;
}

}  // namespace

FUZZ_TEST(in_clone, FuzzingContext &fuzz) {
    Var x("x"), y("y");
    const int W = 8, H = 8;

    PipelineDesc desc = generate_pipeline(fuzz);

    // Reference: build and realize without any wrappers.
    Buffer<int> reference;
    {
        std::vector<Func> funcs;
        build_funcs(desc, x, y, funcs);
        for (int i = 0; i < (int)funcs.size(); i++) {
            if (i != desc.output) {
                funcs[i].compute_root();
            }
        }
        reference = funcs[desc.output].realize({W, H});
    }

    // Test: build the same pipeline, then apply a random sequence of
    // in()/clone_in() operations to grow it into something complex.
    std::vector<Func> funcs;
    build_funcs(desc, x, y, funcs);

    CallGraphModel model;
    for (const NodeDesc &node : desc.nodes) {
        model.add(direct_callees(node));
    }
    const int output = desc.output;

    const int num_ops = fuzz.ConsumeIntegralInRange<int>(5, 40);
    const int max_funcs = 64;
    for (int step = 0; step < num_ops && model.size() < max_funcs; step++) {
        // Pick a wrap target (never the output, which has no callers) and a
        // distinct consumer.
        int target = fuzz.ConsumeIntegralInRange<int>(0, model.size() - 1);
        int consumer = fuzz.ConsumeIntegralInRange<int>(0, model.size() - 1);
        if (target == output || consumer == target) {
            continue;
        }

        std::set<int> resolved = model.resolve(target, consumer);

        // Skip consumers that don't reach the target, and any wrap that would
        // reuse a consumer already wrapped for this target (which is the only
        // way these calls raise a user error).
        if (resolved.empty()) {
            continue;
        }
        bool conflict = false;
        for (int f : resolved) {
            if (model.wrapped_consumers[target].count(f)) {
                conflict = true;
                break;
            }
        }
        if (conflict) {
            continue;
        }

        bool clone = fuzz.ConsumeBool();
        Func wrapper = clone ? funcs[target].clone_in(funcs[consumer])
                             : funcs[target].in(funcs[consumer]);

        // Mirror the rewrite in the model: a clone recomputes what the target
        // computed (same callees); a plain wrapper just reads from the target.
        std::set<int> wrapper_callees = clone ? model.callees[target] : std::set<int>{target};
        int w = model.add(std::move(wrapper_callees));
        model.custom_wrappers[target].insert(w);
        for (int f : resolved) {
            model.wrapped_consumers[target].insert(f);
            // The rewrite substitutes target -> wrapper, so it only redirects a
            // consumer that calls the target directly. A consumer that reaches
            // the target only through an existing wrapper is left unchanged
            // (and the new wrapper is dead), matching Halide.
            if (model.callees[f].erase(target)) {
                model.callees[f].insert(w);
            }
        }

        funcs.push_back(wrapper);
        funcs.back().compute_root();
    }

    Buffer<int> result = funcs[output].realize({W, H});
    return compare(reference, result);
}
