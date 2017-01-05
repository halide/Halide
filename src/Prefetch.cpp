#include <algorithm>
#include <map>
#include <string>

#include "Prefetch.h"
#include "IRMutator.h"
#include "Bounds.h"
#include "Scope.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;

namespace {

// We need to be able to make loads from a buffer that refer to the
// same original image/param/etc. This visitor finds a load to the
// buffer we want to load from, and generates a similar load, but with
// different args.
class MakeSimilarLoad : public IRVisitor {
public:
    const string &buf_name;
    const vector<Expr> &args;
    Expr load;

    MakeSimilarLoad(const string &name, const vector<Expr> &args)
        : buf_name(name), args(args) {}

private:
    using IRVisitor::visit;

    void visit(const Call *op) {
        if (op->name == buf_name) {
            load = Call::make(op->type, op->name, args, op->call_type, op->func, op->value_index, op->image, op->param);
        } else {
            IRVisitor::visit(op);
        }
    }
};

Expr make_similar_load(Stmt s, const string &name, const vector<Expr> &args) {
    MakeSimilarLoad v(name, args);
    s.accept(&v);
    return v.load;
}

// Build a Box representing the bounds of a buffer.
Box buffer_bounds(const string &buf_name, int dims) {
    Box bounds;
    for (int i = 0; i < dims; i++) {
        string dim_name = std::to_string(i);

        Expr buf_min_i = Variable::make(Int(32), buf_name + ".min." + dim_name);
        Expr buf_extent_i = Variable::make(Int(32), buf_name + ".extent." + dim_name);
        Expr buf_max_i = buf_min_i + buf_extent_i - 1;

        bounds.push_back(Interval(buf_min_i, buf_max_i));
    }
    return bounds;
}

class InjectPrefetch : public IRMutator {
public:
    InjectPrefetch(const map<string, Function> &e) : env(e) { }

private:
    const map<string, Function> &env;
    const vector<Prefetch> *prefetches = nullptr;
    Scope<Interval> bounds;

private:
    using IRMutator::visit;

    void visit(const Let *op) {
        Interval in = bounds_of_expr_in_scope(op->value, bounds);
        bounds.push(op->name, in);
        IRMutator::visit(op);
        bounds.pop(op->name);
    }

    void visit(const LetStmt *op) {
        Interval in = bounds_of_expr_in_scope(op->value, bounds);
        bounds.push(op->name, in);
        IRMutator::visit(op);
        bounds.pop(op->name);
    }

    void visit(const ProducerConsumer *op) {
        const vector<Prefetch> *old_prefetches = prefetches;

        map<string, Function>::const_iterator iter = env.find(op->name);
        internal_assert(iter != env.end()) << "function not in environment.\n";
        prefetches = &iter->second.schedule().prefetches();
        IRMutator::visit(op);
        prefetches = old_prefetches;
    }

    Stmt add_prefetch(const string &buf_name, const Box &box, Stmt body) {
        // Construct the bounds to be prefetched.
        vector<Expr> prefetch_min;
        vector<Expr> prefetch_extent;
        for (size_t i = 0; i < box.size(); i++) {
            prefetch_min.push_back(box[i].min);
            prefetch_extent.push_back(box[i].max - box[i].min + 1);
        }

        // Construct an array of index expressions to construct
        // address_of calls with. The first 2 dimensions are handled
        // by (up to) 2D prefetches, the rest we will generate loops
        // to define.
        vector<string> index_names(box.size());
        vector<Expr> indices(box.size());
        for (size_t i = 0; i < box.size(); i++) {
            index_names[i] = "prefetch_" + buf_name + "." + std::to_string(i);
            indices[i] = i < 2 ? prefetch_min[i] : Variable::make(Int(32), index_names[i]);
        }

        // Make a load at the index and get the address.
        Expr prefetch_load = make_similar_load(body, buf_name, indices);
        internal_assert(prefetch_load.defined());
        Type type = prefetch_load.type();
        Expr prefetch_addr = Call::make(Handle(), Call::address_of, {prefetch_load}, Call::Intrinsic);

        Stmt prefetch;
        Expr stride_0 = Variable::make(Int(32), buf_name + ".stride.0");
        // TODO: This is inefficient if stride_0 != 1, because memory
        // potentially not accessed will be prefetched, and it will be
        // fetched multiple times. The right way to handle this would
        // be to set up a prefetch for each individual element of the
        // buffer, in case it is sparse, and then try to optimize the
        // prefetch to fetch dense ranges of addresses. This is hard
        // to do statically.
        Expr extent_0_bytes = prefetch_extent[0] * stride_0 * type.bytes();
        if (box.size() == 1) {
            // The prefetch is only 1 dimensional, just emit a flat prefetch.
            prefetch = Evaluate::make(Call::make(Int(32), Call::prefetch,
                                                 {prefetch_addr, extent_0_bytes},
                                                 Call::PureIntrinsic));
        } else {
            // Make a 2D prefetch.
            Expr stride_1 = Variable::make(Int(32), buf_name + ".stride.1");
            Expr stride_1_bytes = stride_1 * type.bytes();
            prefetch = Evaluate::make(Call::make(Int(32), Call::prefetch_2d,
                                                 {prefetch_addr, extent_0_bytes, prefetch_extent[1], stride_1_bytes},
                                                 Call::PureIntrinsic));

            // Make loops for the rest of the dimensions (possibly zero).
            for (size_t i = 2; i < box.size(); i++) {
                prefetch = For::make(index_names[i], prefetch_min[i], prefetch_extent[i],
                                     ForType::Serial, DeviceAPI::None,
                                     prefetch);
            }
        }

        // We should only prefetch buffers that are used.
        if (box.maybe_unused()) {
            prefetch = IfThenElse::make(box.used, prefetch);
        }

        return Block::make({prefetch, body});
    }

    void visit(const For *op) {
        // Add loop variable to interval scope for any inner loop prefetch
        Expr loop_var = Variable::make(Int(32), op->name);
        bounds.push(op->name, Interval(loop_var, loop_var));
        Stmt body = mutate(op->body);
        bounds.pop(op->name);

        if (prefetches) {
            for (const Prefetch &p : *prefetches) {
                if (!ends_with(op->name, "." + p.var)) {
                    continue;
                }
                // Add loop variable + prefetch offset to interval scope for box computation
                Expr fetch_at = loop_var + p.offset;
                bounds.push(op->name, Interval(fetch_at, fetch_at));
                map<string, Box> boxes_read = boxes_required(body, bounds);
                bounds.pop(op->name);

                // Don't prefetch buffers that are written to. We assume that these already
                // have good locality.
                // TODO: This is not a good assumption. It would be better to have the
                // prefetch directive specify the buffer that we want to prefetch, instead
                // of trying to figure out which buffers should be prefetched. This would also
                // mean that we don't need the "make_similar_load" hack, because we can make
                // calls the standard way (using the ImageParam/Function object referenced in
                // the prefetch).
                map<string, Box> boxes_written = boxes_provided(body, bounds);
                for (const auto &b : boxes_written) {
                    auto it = boxes_read.find(b.first);
                    if (it != boxes_read.end()) {
                        debug(2) << "Not prefetching buffer " << it->first
                                 << " also written in loop " << op->name << "\n";
                        boxes_read.erase(it);
                    }
                }

                // TODO: Only prefetch the newly accessed data from the previous iteration.
                // This should use boxes_touched (instead of boxes_required) so we exclude memory
                // either read or written.
                for (const auto &b : boxes_read) {
                    const string &buf_name = b.first;

                    // Only prefetch the region that is in bounds.
                    Box bounds = buffer_bounds(buf_name, b.second.size());
                    Box prefetch_box = box_intersection(b.second, bounds);

                    body = add_prefetch(buf_name, prefetch_box, body);
                }
            }
        }

        if (!body.same_as(op->body)) {
            stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
        } else {
            stmt = op;
        }
    }

};

} // namespace

Stmt inject_prefetch(Stmt s, const map<string, Function> &env) {
    return InjectPrefetch(env).mutate(s);
}

}
}
