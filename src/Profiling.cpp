#include "Profiling.h"
#include "IRMutator.h"
#include "IROperator.h"
#include <algorithm>
#include <map>
#include <string>

namespace Halide {
namespace Internal {

namespace {
    const char kBufName[] = "ProfilerBuffer";
    const char kToplevel[] = "$total$";
}

int profiling_level() {
    char *trace = getenv("HL_PROFILE");
    return trace ? atoi(trace) : 0;
}

using std::map;
using std::string;
using std::vector;

class InjectProfiling : public IRMutator {
public:
    InjectProfiling(string func_name)
        : level(profiling_level()),
          func_name(sanitize(func_name)) {
    }

    Stmt inject(Stmt s) {
        if (level >= 1) {
            // Add calls to the usec and timer at the start and end,
            // so that we can get an estimate of how long a single
            // tick of the profiling_timer is (since it may be dependent
            // on e.g. processor clock rate)
            {
                PushCallStack st(this, kToplevel, kToplevel);
                s = mutate(s);
            }
            s = add_ticks(kToplevel, kToplevel, s);
            s = add_usec(kToplevel, kToplevel, s);

            // Note that this is tacked on to the front of the block, since it must come
            // before the calls to halide_current_time_usec.
            Expr begin_clock_call = Call::make(Int(32), "halide_start_clock", std::vector<Expr>(), Call::Extern);
            Stmt begin_clock = AssertStmt::make(begin_clock_call == 0, "Failed to start clock");
            s = Block::make(begin_clock, s);

            // Tack on code to print the counters.
            for (map<string, int>::const_iterator it = indices.begin(); it != indices.end(); ++it) {
                int idx = it->second;
                Expr val = Load::make(UInt(64), kBufName, idx, Buffer(), Parameter());
                Stmt print_val = PrintStmt::make(it->first, vec(val));
                s = Block::make(s, print_val);
            }

            // Now that we know the final size, allocate the buffer and init to zero.
            Expr i = Variable::make(Int(32), "i");
            Stmt init = For::make("i", 0, (int)indices.size(), For::Serial,
                Store::make(kBufName, Cast::make(UInt(64), 0), i));
            s = Block::make(init, s);

            s = Allocate::make(kBufName, UInt(64), (int)indices.size(), s);
        } else {
            s = mutate(s);
        }
        return s;
    }

private:
    using IRMutator::visit;

    const int level;
    const string func_name;
    map<string, int> indices;   // map name -> index in buffer.
    vector<string> call_stack;  // names of the nodes upstream

    class PushCallStack {
    public:
        InjectProfiling* ip;

        PushCallStack(InjectProfiling* ip, const string& op_type, const string& op_name) : ip(ip) {
            ip->call_stack.push_back(op_type + " " + op_name);
        }

        ~PushCallStack() {
            ip->call_stack.pop_back();
        }
    };

    // replace all spaces with '_'
    static string sanitize(const string& s) {
      string san = s;
      std::replace(san.begin(), san.end(), ' ', '_');
      return san;
    }

    Expr get_index(const string& s) {
        if (indices.find(s) == indices.end()) {
            int idx = indices.size();
            indices[s] = idx;
        }
        return indices[s];
    }

    Stmt add_count_and_ticks(const string& op_type, const string& op_name, Stmt s) {
        s = add_count(op_type, op_name, s);
        s = add_ticks(op_type, op_name, s);
        return s;
    }

    Stmt add_count(const string& op_type, const string& op_name, Stmt s) {
        return add_delta("count", op_type, op_name, Cast::make(UInt(64), 0), Cast::make(UInt(64), 1), s);
    }

    Stmt add_ticks(const string& op_type, const string& op_name, Stmt s) {
        Expr ticks = Call::make(UInt(64), Internal::Call::profiling_timer, vector<Expr>(), Call::Intrinsic);
        return add_delta("ticks", op_type, op_name, ticks, ticks, s);
    }

    Stmt add_usec(const string& op_type, const string& op_name, Stmt s) {
        Expr usec = Call::make(UInt(64), "halide_current_time_usec", std::vector<Expr>(), Call::Extern);
        return add_delta("usec", op_type, op_name, usec, usec, s);
    }

    Stmt add_delta(const string& metric_name, const string& op_type, const string& op_name,
                   Expr begin_val, Expr end_val, Stmt s) {
        string parent_name_pair = call_stack.empty() ?
            "null null" :
            call_stack.back();
        string full_name = "halide_profiler " +
            metric_name + " " +
            func_name + " " +
            op_type + " " +
            sanitize(op_name) + " " +
            parent_name_pair;
        assert(begin_val.type() == UInt(64));
        assert(end_val.type() == UInt(64));
        Expr idx = get_index(full_name);

        string begin_var_name = "begin_" + full_name;
        // variable name doesn't matter at all, but de-spacing
        // makes the Stmt output easier to read
        std::replace(begin_var_name.begin(), begin_var_name.end(), ' ', '_');
        Expr begin_var = Variable::make(UInt(64), begin_var_name);

        Expr old_val = Load::make(UInt(64), kBufName, idx, Buffer(), Parameter());
        Expr delta_val = Sub::make(end_val, begin_var);
        Expr new_val = Add::make(old_val, delta_val);
        s = Block::make(s, Store::make(kBufName, new_val, idx));
        s = LetStmt::make(begin_var_name, begin_val, s);

        return s;
    }

    void visit(const Pipeline *op) {
        if (level >= 1) {
            Stmt produce, update, consume;
            {
                PushCallStack st(this, "produce", op->name);
                produce = mutate(op->produce);
            }
            {
                PushCallStack st(this, "update", op->name);
                update = op->update.defined() ? mutate(op->update) : Stmt();
            }
            {
                PushCallStack st(this, "consume", op->name);
                consume = mutate(op->consume);
            }

            produce = add_count_and_ticks("produce", op->name, produce);
            update = update.defined() ? add_count_and_ticks("update", op->name, update) : Stmt();
            consume = add_count_and_ticks("consume", op->name, consume);

            stmt = Pipeline::make(op->name, produce, update, consume);
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const For *op) {
        IRMutator::visit(op);
        if (level >= 1) {
            if (op->for_type == For::Parallel) {
                assert(false && "Halide Profiler does not yet support parallel schedules. "
                    "try removing parallel() schedules and re-running.");
            }
        }
        // We only instrument loops at profiling level 2 or higher
        if (level >= 2) {
            stmt = add_count_and_ticks("forloop", op->name, stmt);
        }
    }
};

Stmt inject_profiling(Stmt s, string name) {
    InjectProfiling profiling(name);
    s = profiling.inject(s);
    return s;
}

}
}
