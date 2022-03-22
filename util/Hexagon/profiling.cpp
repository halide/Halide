/** \file
 * TODO
 */

#include "HAP_farf.h"
#include "HAP_perf.h"

#include "AEEStdErr.h"

#include "hexagon-dsp.h"
#include "hexagon_protos.h"
#include "hexagon_types.h"

#include "HalideRuntime.h"
#include "HalideRuntimeHexagonHost.h"

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <thread>
#include <vector>

#include "qurt.h"

#define DSP_SUCCESS 0
#define DSP_FAILURE -1

#define NONE 0
#define SAFETY 1
#define VERBOSE 2

#define DEBUG_LEVEL SAFETY

#define debug(LEVEL) if (DEBUG_LEVEL >= LEVEL)

#define println(args...)                                                       \
  {                                                                            \
    char str[512] = {0};                                                       \
    sprintf(str, args);                                                        \
    HAP_debug(str, 2, "profiler", __LINE__);                                   \
  }
#define error(args...)                                                         \
  {                                                                            \
    char str[512] = {0};                                                       \
    sprintf(str, args);                                                        \
    HAP_debug(str, 2, "profiler", __LINE__);                                   \
    exit(-1);                                                                  \
  }

// Wrapper class which automatically guards the contained structure with a mutex
// lock
template <class X> struct Atomic {
  template <class... Args>
  Atomic(Args &&...args) : x(std::forward<Args>(args)...) {}

  template <class F> auto operator()(F f) -> decltype(auto) {
    auto lock = std::lock_guard(m);
    return f(x);
  }

private:
  std::mutex m;
  X x;
};

// Tracks timing data for a Halide Loop
// Owns its sub-loops
struct Loop {
  using Id = uint32_t;
  Loop(Id id, const char *label)
      : loop_id(id), thread_id(qurt_thread_get_id()), label(label) {}

  const Id loop_id;
  const qurt_thread_t thread_id;
  const char *label;

  auto operator<(const Loop &that) const -> bool {
    return std::tuple(loop_id, thread_id) <
           std::tuple(that.loop_id, that.thread_id);
  }

  // construction/retrieval
  auto emplace(Id loop_id, const char *label) -> Loop & {
    // comparison keys are const even if instance is mutable,
    // so this cast is safe
    return const_cast<Loop &>(*std::get<0>(_children.emplace(loop_id, label)));
  }
  auto get_or_emplace(Id loop_id, const char *label) -> Loop & {
    for (auto &loop : _children) {
      const auto this_thread_id = qurt_thread_get_id();
      if (loop_id == loop.loop_id and loop.thread_id == this_thread_id) {
        // comparison keys are const even if instance is mutable,
        // so this cast is safe
        return const_cast<Loop &>(loop);
      }
    }
    return emplace(loop_id, label);
  }

  // timing
  void start_timer() { _last_qtimer_count = HAP_perf_get_qtimer_count(); }
  void stop_timer() {
    _accumulated_qtimer_count +=
        HAP_perf_get_qtimer_count() - _last_qtimer_count;
    ++_invocation_count;
  }
  void record_overhead(uint64_t qtimer_count) {
    _overhead_qtimer_count += qtimer_count;
  }

  // reporting
  auto accumulated_microseconds() const -> uint64_t {
    return HAP_perf_qtimer_count_to_us(_accumulated_qtimer_count);
  }
  auto times_called() const -> uint32_t { return _invocation_count; }
  auto overhead_microseconds() const -> uint64_t {
    return HAP_perf_qtimer_count_to_us(_overhead_qtimer_count);
  }

  // visitors
  void for_each_height(std::function<void(const Loop &, uint32_t)> body,
                       uint32_t height = 0) const {
    body(*this, height);
    for (const auto &child : _children) {
      child.for_each_height(body, height + 1);
    }
  }
  void for_each(std::function<void(const Loop &)> body) const {
    for_each_height([&](auto &g, auto) { body(g); });
  }

private:
  uint32_t _invocation_count = 0;
  uint64_t _accumulated_qtimer_count = 0;
  uint64_t _last_qtimer_count = 0;
  uint64_t _overhead_qtimer_count = 0;

  std::set<Loop> _children;
};
// Per-thread stack of Loops, tracks current state of control flow and push/pop
// time of Loops. Does not own its loops
struct Thread {
  void push(Loop &loop) {
    _stack.emplace_back(loop);

    loop.start_timer();
  }
  void pop() {
    auto &loop = top();

    loop.stop_timer();

    _stack.pop_back();

    debug(VERBOSE) if (_stack.empty()) { println("stack exhausted!"); }
  }
  auto top() -> Loop & {
    debug(SAFETY) if (_stack.empty()) {
      error("request to access current node in exhausted cfg traversal");
    }

    return _stack.back().get();
  }

private:
  std::vector<std::reference_wrapper<Loop>> _stack;
};
// Container for program metadata, recorded at the entry point
struct Metadata {
  Metadata(const char *name) : name(name) {}

  const char *const name;

  template <class T>
  void append_arg(const char *name, T value, const char *type, bool is_output) {
    auto &args = [this, is_output]() -> std::vector<std::vector<char>> & {
      if (is_output) {
        return _output_args;
      } else {
        return _input_args;
      }
    }();
    args.emplace_back();
    auto &line = args.back();
    line.resize(512);
    int len = 0;
    len += sprintf(&line[len], "%c", is_output ? '<' : '>');
    if constexpr (std::is_same_v<T, halide_buffer_t *>) {
      const auto &buf = value;
      len += sprintf(&line[len], "%s = [", name);
      for (int i = 0; i < buf->dimensions; ++i) {
        len += sprintf(&line[len], "%li", buf->dim[i].extent - buf->dim[i].min);
        if (i + 1 < buf->dimensions) {
          len += sprintf(&line[len], "][");
        }
      }
      len += sprintf(&line[len], "] : %s", type);
      line.resize(len);
    } else {
      const char *fmt = [] {
        if constexpr (std::is_floating_point_v<T>) {
          return "%s = %f : %s";
        } else if constexpr (std::is_integral_v<T>) {
          return "%s = %i : %s";
        } else {
          error("%s does not know how to handle param type",
                __PRETTY_FUNCTION__);
        }
      }();

      line.resize(sprintf(&line[len], fmt, name, value, type));
    }
  }

  void describe_schedule(const char *sched) { _sched.push_back(sched); }

  void print_signature() {
    println("%s", name);
    for (const auto &arg : _input_args) {
      println("%s", &arg.front());
    }
    for (const auto &arg : _output_args) {
      println("%s", &arg.front());
    }
  }
  void print_schedule() {
    for (auto &line : _sched) {
      println("%s", line);
    }
  }

private:
  std::vector<std::vector<char>> _input_args;
  std::vector<std::vector<char>> _output_args;
  std::vector<const char *> _sched;
};

// Main profiling state class
// Data structures expected to be accessed together have been grouped here in
// order to stay behind a single mutex
struct ControlFlow {
  // Profiling statistics tree
  std::unique_ptr<Loop> root;
  // Per-thread stack tracking
  std::map<qurt_thread_t, Thread> threads;
  // Per-thread stack of thread launch points
  std::map<qurt_thread_t, std::vector<std::reference_wrapper<Loop>>>
      fork_points;
};

static std::optional<Metadata> metadata;
static Atomic<std::map<qurt_thread_t, std::vector<char>>> thread_table; // thread names for pretty-printing
static Atomic<ControlFlow> ctrl_flow;
thread_local std::optional<std::reference_wrapper<Thread>> this_thread;

// Register a thread with the global thread_table.
auto register_thread() -> qurt_thread_t {
  const auto tid = qurt_thread_get_id();

  thread_table([&](auto &threads) {
    if (not threads.count(tid)) {
      auto &name = threads[tid] = std::vector<char>(64);
      qurt_thread_get_name(name.data(), name.size());
      auto n = strlen(name.data());
      if (n == 0) {
        sprintf(name.data(), "thread-%u", tid);
        n = strlen(name.data());
      }
      name.resize(n + 1);
    }
  });

  return tid;
}

// Halide-accessible API
extern "C" { // metadata
void declare_generator(const char *name) { metadata.emplace(Metadata{name}); }
void describe_schedule(const char *schedule) {
  metadata->describe_schedule(schedule);
}
void trace_parameter_float(const char *name, float value, const char *type,
                           bool is_output) {
  metadata->append_arg(name, value, type, is_output);
}
void trace_parameter_int(const char *name, int value, const char *type,
                         bool is_output) {
  metadata->append_arg(name, value, type, is_output);
}
void trace_parameter_buffer(const char *name, halide_buffer_t *buf,
                            bool is_output) {
  static std::list<std::vector<char>> strings;
  strings.emplace_back();
  auto &typestr = strings.back();

  typestr.resize(64);
  int len = 0;
  const char *base;
  switch (buf->type.code) {
    break;
  case halide_type_int:
    base = "int";
    break;
  case halide_type_uint:
    base = "uint";
    break;
  case halide_type_float:
    base = "float";
    break;
  case halide_type_handle:
    base = "handle";
    break;
  case halide_type_bfloat:
    base = "bfloat";
    break;
  default:
    error("unknown type code %u", buf->type.code);
  }

  len += sprintf(&typestr[len], "%s%u", base, buf->type.bits);
  if (auto lanes = buf->type.lanes; lanes > 1) {
    len += sprintf(&typestr[len], "x%u", buf->type.lanes);
  }
  typestr.resize(len);

  metadata->append_arg(name, buf, typestr.data(), is_output);
}
}
extern "C" { // control flow
void program_start(uint32_t root_loop_id, const char *label) {
  debug(VERBOSE) { println("%s %lu %s", __FUNCTION__, root_loop_id, label); }
  thread_table([](auto &threads) { threads.clear(); });
  const auto tid = register_thread();
  auto [thread, root] = ctrl_flow([&](auto &ctrl) {
    ctrl.root = std::make_unique<Loop>(root_loop_id, label);
    return std::tuple(
        // we can't do anything to the key after the call to std::get,
        // so this cast is safe
        std::ref(const_cast<Thread &>(
            std::get<1>(*std::get<0>(ctrl.threads.emplace(tid, Thread{}))))),
        std::ref(*ctrl.root));
  });
  this_thread.emplace(thread.get());
  this_thread->get().push(root.get());
}
void program_end() {
  debug(VERBOSE) { println(__FUNCTION__); }
  this_thread->get().pop();
}
auto get_thread_id() -> uint32_t {
  debug(VERBOSE) { println(__FUNCTION__); }
  return qurt_thread_get_id();
}
void pre_fork(uint32_t loop_id, const char *label) {
  const auto start_time = HAP_perf_get_qtimer_count();
  this_thread->get().push(
      this_thread->get().top().get_or_emplace(loop_id, label));
  ctrl_flow([](auto &ctrl) {
    ctrl.fork_points[get_thread_id()].push_back(this_thread->get().top());
  });
  this_thread->get().top().record_overhead(HAP_perf_get_qtimer_count() -
                                           start_time);
}
void post_fork() {
  const auto start_time = HAP_perf_get_qtimer_count();
  ctrl_flow([](auto &ctrl) { ctrl.fork_points[get_thread_id()].pop_back(); });
  this_thread->get().top().record_overhead(HAP_perf_get_qtimer_count() -
                                           start_time);
  this_thread->get().pop();
}
void fork_start(uint32_t parent_thread, uint32_t loop_id, const char *label) {
  debug(VERBOSE) {
    println("%s from parent thread %lu into loop %lu: %s", __FUNCTION__,
            parent_thread, loop_id, label);
  }
  const auto start_time = HAP_perf_get_qtimer_count();
  const auto tid = register_thread();
  auto [forked_thread, forked_loop] = ctrl_flow([&](auto &ctrl) {
    return std::tuple(std::ref(ctrl.threads[tid]),
                      std::ref(ctrl.fork_points.at(parent_thread)
                                   .back()
                                   .get()
                                   .emplace(loop_id, label)));
  });
  this_thread.emplace(forked_thread.get());
  forked_thread.get().push(forked_loop.get());
  this_thread->get().top().record_overhead(HAP_perf_get_qtimer_count() -
                                           start_time);
}
void fork_end() {
  debug(VERBOSE) { println(__FUNCTION__); }
  this_thread->get().pop();
}
void loop_start(uint32_t id, const char *label) {
  debug(VERBOSE) { println("%s %lu %s", __FUNCTION__, id, label); }
  auto &cfg = this_thread->get();
  cfg.push(cfg.top().get_or_emplace(id, label));
}
void loop_end() {
  debug(VERBOSE) { println(__FUNCTION__); }
  this_thread->get().pop();
}
}
extern "C" { // reporting
void print_report() {
  std::size_t i = 0;
  println("-------------------- begin");
  metadata->print_signature();
  println("--------------------");
  metadata->print_schedule();
  println("--------------------");
  thread_table([](const auto &table) {
    for (auto &[tid, name] : table) {

      println("%u %s", tid, name.data());
    }
  });
  println("--------------------");
  ctrl_flow([](auto &ctrl) {
    ctrl.root->for_each_height([&](const Loop &loop, uint32_t height) {
      char output[512] = {0};
      for (auto i = 0; i < height; ++i) {
        output[i] = '>';
      }
      sprintf(output + height, "%s %u %llu %lu %llu", loop.label,
              loop.thread_id, loop.accumulated_microseconds(),
              loop.times_called(), loop.overhead_microseconds());
      println("%s", output);
    });
  });
  println("-------------------- end");
}
}
