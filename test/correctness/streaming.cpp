#include "Halide.h"

#include <algorithm>
#include <cstdio>
#ifdef TEST_WITH_SERIALIZATION
#include <map>
#endif
#include <string>
#include <utility>
#include <vector>

using namespace Halide;
using namespace Halide::Internal;

namespace {

class CheckStreamingAccesses : public IRMutator {
public:
    using IRMutator::mutate;

    Stmt mutate(const Stmt &s) override {
        class Finder : public IRVisitor {
        public:
            // (name, is_streaming) pairs, in the order they're visited (which,
            // for a given name, is the order the Stages that touch it run in).
            std::vector<std::pair<std::string, bool>> loads;
            std::vector<std::pair<std::string, bool>> stores;
            int stream_store_fences = 0;

        protected:
            using IRVisitor::visit;

            void visit(const Load *op) override {
                loads.emplace_back(op->name, op->is_streaming);
                IRVisitor::visit(op);
            }

            void visit(const Store *op) override {
                stores.emplace_back(op->name, op->is_streaming);
                IRVisitor::visit(op);
            }

            void visit(const Call *op) override {
                if (op->is_intrinsic(Call::stream_store_fence)) {
                    stream_store_fences++;
                }
                IRVisitor::visit(op);
            }
        } finder;

        s.accept(&finder);

        auto has_access = [](const std::vector<std::pair<std::string, bool>> &accesses,
                             const std::string &name, bool streaming) {
            for (const auto &a : accesses) {
                if (a.first == name && a.second == streaming) {
                    return true;
                }
            }
            return false;
        };

        auto require = [&](const std::vector<std::pair<std::string, bool>> &accesses,
                           const std::string &name, bool streaming, const char *kind) {
            if (!has_access(accesses, name, streaming)) {
                std::fprintf(stderr, "Expected a %s access to %s\n", kind, name.c_str());
                std::exit(1);
            }
        };

        require(finder.loads, "input_a", true, "streaming load");
        require(finder.loads, "streaming_producer", true, "streaming load");
        require(finder.stores, "streaming_producer", true, "streaming store");
        require(finder.stores, "output", true, "streaming store");
        require(finder.loads, "input_b", false, "ordinary load");
        require(finder.loads, "ordinary_producer", false, "ordinary load");
        require(finder.stores, "ordinary_producer", false, "ordinary store");

        // self_referencing has two Stages: a pure definition (no self-load)
        // followed by an update that self-loads. Only the update Stage was
        // scheduled with stream_stores() (see below), so the pure
        // definition's store is ordinary. The update Stage has no RVar, so
        // it's legal to stream, and since the fence for a streamed Stage is
        // emitted immediately after that Stage's own production, the
        // update's self-load (of the pure definition's already-visible,
        // ordinary data) is unaffected. A downstream consumer's read is not
        // a self-load, so it may still stream.
        //
        // Each Stage's store may appear more than once (e.g. a vectorized
        // main loop plus a scalar tail from loop partitioning), so check
        // that the sequence is "some ordinary stores, then some streaming
        // stores" rather than requiring an exact count.
        std::vector<bool> self_referencing_stores;
        for (const auto &store : finder.stores) {
            if (store.first == "self_referencing") {
                self_referencing_stores.push_back(store.second);
            }
        }
        bool well_formed = !self_referencing_stores.empty() &&
                           self_referencing_stores.front() == false &&
                           self_referencing_stores.back() == true &&
                           std::is_sorted(self_referencing_stores.begin(), self_referencing_stores.end());
        if (!well_formed) {
            std::fprintf(stderr, "Expected self_referencing's pure definition to store ordinarily "
                                 "and its update to store streamingly\n");
            std::exit(1);
        }
        require(finder.loads, "self_referencing", false, "ordinary load");  // the update's self-load
        require(finder.loads, "self_referencing", true, "streaming load");  // output's downstream read

        // One fence each for streaming_producer, self_referencing, and output.
        if (finder.stream_store_fences != 3) {
            std::fprintf(stderr, "Expected exactly 3 streaming store fences, found %d\n",
                         finder.stream_store_fences);
            std::exit(1);
        }

        return s;
    }
};

}  // namespace

int main(int argc, char **argv) {
    Var x;
    ImageParam input_a(Int(32), 1, "input_a");
    ImageParam input_b(Int(32), 1, "input_b");
    Func streaming_producer("streaming_producer");
    Func ordinary_producer("ordinary_producer");
    Func self_referencing("self_referencing");
    Func output("output");

    streaming_producer(x) = input_a(x) + 1;
    ordinary_producer(x) = input_b(x) + 2;

    // An update definition with a self-load.
    self_referencing(x) = 0;
    self_referencing(x) += x;

    output(x) = streaming_producer(x) + ordinary_producer(x) + self_referencing(x);

    streaming_producer.compute_root().stream_stores();
    // An ImageParam can be named directly: it's resolved to the underlying
    // Parameter, since the ImageParam's own wrapper Func is inlined away
    // before storage flattening ever sees it.
    streaming_producer.stream_loads({input_a});
    ordinary_producer.compute_root();
    self_referencing.compute_root();
    self_referencing.update(0).stream_stores();
    output.stream_stores();
    // stream_loads() is a property of the reading Stage, not the Func being
    // read: output is the one that directly reads streaming_producer and
    // self_referencing, so it's the one that requests streaming those two
    // reads (ordinary_producer's read is deliberately left off this list).
    output.stream_loads({streaming_producer, self_referencing});

    streaming_producer.vectorize(x, 8);
    ordinary_producer.vectorize(x, 8);
    self_referencing.vectorize(x, 8);
    self_referencing.update(0).vectorize(x, 8);
    output.vectorize(x, 8);

    constexpr int size = 1024;
    Buffer<int32_t> a(size), b(size);
    for (int i = 0; i < size; i++) {
        a(i) = i;
        b(i) = 3 * i;
    }
    input_a.set(a);
    input_b.set(b);

    Pipeline pipeline(output);
#ifdef TEST_WITH_SERIALIZATION
    // Verify that the access directives survive pipeline serialization as
    // well as the normal lowering pipeline.
    std::vector<uint8_t> serialized;
    std::map<std::string, Parameter> params;
    serialize_pipeline(pipeline, serialized, params);
    pipeline = deserialize_pipeline(serialized, params);
#endif
    pipeline.add_custom_lowering_pass(new CheckStreamingAccesses);

    Buffer<int32_t> result = pipeline.realize({size});
    for (int i = 0; i < size; i++) {
        if (result(i) != 5 * i + 3) {
            std::fprintf(stderr, "Incorrect result at %d: %d\n", i, result(i));
            return 1;
        }
    }

    std::printf("Success!\n");
    return 0;
}
