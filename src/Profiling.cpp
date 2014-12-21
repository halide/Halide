#include <algorithm>
#include <map>
#include <string>

#include "Profiling.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

namespace {
    const char kBufName[] = "ProfilerBuffer";
    const char kToplevel[] = "$total$";
    const char kOverhead[] = "$overhead$";
    const char kIgnore[] = "$ignore$";
    const char kIgnoreBuf[] = "$ignore_buf$";
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
            // Add calls to the nsec and timer at the start and end,
            // so that we can get an estimate of how long a single
            // tick of the profiling_timer is (since it may be dependent
            // on e.g. processor clock rate)
            {
                PushCallStack st(this, kToplevel, kToplevel);
                s = mutate(s);
            }
            s = add_ticks(kToplevel, kToplevel, s);
            s = add_nsec(kToplevel, kToplevel, s);

            // Note that this is tacked on to the front of the block, since it must come
            // before the calls to halide_current_time_ns.
            Expr begin_clock_call = Call::make(Int(32), "halide_start_clock", vector<Expr>(), Call::Extern);
            Stmt begin_clock = AssertStmt::make(begin_clock_call == 0, "Failed to start clock");
            s = Block::make(begin_clock, s);

            // Do a little calibration: make a loop that does a large number of calls to add_ticks
            // and measures the total time, so we can calculate the average overhead
            // and subtract it from the final results. (The "body" of this loop is just
            // a store of 0 to scratch that we expect to be optimized away.) This isn't a perfect
            // solution, but is much better than nothing.
            //
            // Note that we deliberately unroll a bit to minimize loop overhead, otherwise our
            // estimate will be too high.
            //
            // NOTE: we deliberately do this *after* measuring
            // the total, so this should *not* be included in "kToplevel".
            const int kIters = 1000000;
            const int kUnroll = 4;
            Stmt ticker_block = Stmt();
            for (int i = 0; i < kUnroll; i++) {
                ticker_block = Block::make(
                    add_ticks(kIgnore, kIgnore, Store::make(kIgnoreBuf, Cast::make(UInt(32), 0), 0)),
                        ticker_block);
            }

            Expr j = Variable::make(Int(32), "j");
            Stmt do_timings = For::make("j", 0, kIters, For::Serial, DeviceAPI::Host, ticker_block);
            do_timings = add_ticks(kOverhead, kOverhead, do_timings);
            do_timings = add_delta("count", kOverhead, kOverhead, Cast::make(UInt(64), 0),
                Cast::make(UInt(64), kIters * kUnroll), do_timings);
            s = Block::make(s, do_timings);
            s = Allocate::make(kIgnoreBuf, UInt(32), vec(Expr(1)), const_true(), s);

            // Tack on code to print the counters.
            for (map<string, int>::const_iterator it = indices.begin(); it != indices.end(); ++it) {
                int idx = it->second;
                Expr val = Load::make(UInt(64), kBufName, idx, Buffer(), Parameter());
                Expr print_val = print(it->first, val);
                Stmt print_stmt = Evaluate::make(print_val);
                s = Block::make(s, print_stmt);
            }

            // Now that we know the final size, allocate the buffer and init to zero.
            Expr i = Variable::make(Int(32), "i");
            Stmt init = For::make("i", 0, (int)indices.size(), For::Serial, DeviceAPI::Host,
                Store::make(kBufName, Cast::make(UInt(64), 0), i));
            s = Block::make(init, s);

            s = Allocate::make(kBufName, UInt(64), vec(Expr((int)indices.size())), const_true(), s);
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

    Stmt add_nsec(const string& op_type, const string& op_name, Stmt s) {
        Expr nsec = Call::make(UInt(64), "halide_current_time_ns", std::vector<Expr>(), Call::Extern);
        return add_delta("nsec", op_type, op_name, nsec, nsec, s);
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
        internal_assert(begin_val.type() == UInt(64));
        internal_assert(end_val.type() == UInt(64));
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
        if (op->for_type == For::Parallel && level >= 1) {
            std::cerr << "Warning: The Halide profiler does not yet support "
                      << "parallel schedules. Not profiling inside the loop over "
                      << op->name << "\n";
            stmt = op;
        } else {
            IRMutator::visit(op);
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
