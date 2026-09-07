#ifndef HALIDE_PROFILED_IR_VISITOR_H
#define HALIDE_PROFILED_IR_VISITOR_H

#ifdef WITH_COMPILER_PROFILING

#include "IR.h"

#include <algorithm>
#include <chrono>
#include <list>
#include <mutex>
#include <thread>

#ifndef __FUNCTION_NAME__
#ifdef WIN32  // WINDOWS
#define __FUNCTION_NAME__ __FUNCTION__
#else  //*NIX
#define __FUNCTION_NAME__ __func__
#endif
#endif

/** \file
 * Defines the base class for things that recursively walk over the IR
 */

namespace Halide {
namespace Internal {
namespace Profiling {

constexpr uint8_t BIT_GENERIC = 1 << 0;
constexpr uint8_t BIT_STMT = 1 << 1;
constexpr uint8_t BIT_EXPR = 1 << 2;

inline uint64_t performance_counter() {
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

struct Event {
    const char *src_tag;
    uint32_t timer;
    uint32_t event_type : 1;  // 0 for Start, 1 for Stop

    enum Tag : uint32_t {
        Generic,
        Visitor,
    } tag : 1;

    /** Optional for visitors and mutators. */
    uint32_t data : 30;
};
static_assert(sizeof(Event) == sizeof(void *) + 8);

struct ThreadTrace {
    uint32_t tid;
    uint64_t start_cycles_64;  // 64-bit anchor to align threads accurately
    std::vector<Event> events;
};

struct Context {
    std::mutex mutex;
    uint8_t active_bits{0};

    // std::list guarantees that pointers to elements are never invalidated
    std::list<ThreadTrace> traces;

    Context();
    ~Context();
};

inline Context ctx;

inline ThreadTrace &init_thread_profiler() {
    std::lock_guard<std::mutex> lock(ctx.mutex);

    // Hash thread ID to get a clean 32-bit integer for the Chrome UI
    uint32_t tid = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    uint64_t anchor = performance_counter();

    ctx.traces.push_back({tid, anchor, {}});
    ThreadTrace &tt = ctx.traces.back();

    // Pre-allocate ~16MB per thread to guarantee zero allocations during hot loops
    tt.events.reserve(1'000'000);

    return tt;
}

inline thread_local ThreadTrace &tls_thread_trace = init_thread_profiler();
inline thread_local std::vector<Event> *tls_profiling_events = &tls_thread_trace.events;

inline void zone_begin(const char *src_tag, Event::Tag tag, unsigned data = 0) {
    Event pe;
    pe.timer = static_cast<uint32_t>(performance_counter());
    pe.tag = tag;
    pe.src_tag = src_tag;
    pe.event_type = 0;  // Start
    pe.data = data;
    tls_profiling_events->push_back(pe);
}

inline void zone_end(const char *src_tag, Event::Tag tag, unsigned data = 0) {
    Event pe;
    pe.timer = static_cast<uint32_t>(performance_counter());
    pe.tag = tag;
    pe.src_tag = src_tag;
    pe.event_type = 1;  // Stop
    pe.data = data;
    tls_profiling_events->push_back(pe);
}

inline void generic_zone_begin(const char *src_tag, unsigned data = 0) {
    if (ctx.active_bits & BIT_GENERIC) {
        zone_begin(src_tag, Event::Tag::Generic, data);
    }
}

inline void generic_zone_end(const char *src_tag, unsigned data = 0) {
    if (ctx.active_bits & BIT_GENERIC) {
        zone_end(src_tag, Event::Tag::Generic, data);
    }
}

struct ZoneScopedVisitor_ {
    IRNodeType node_type;
    const char *src_tag;
    bool active;

    ZoneScopedVisitor_(IRNodeType node_type, const char *src_tag, uint8_t activation_bits)
        : node_type(node_type), src_tag(src_tag), active(ctx.active_bits & activation_bits) {
        if (active) {
            zone_begin(src_tag, Event::Visitor, (unsigned)node_type);
        }
    }

    ZoneScopedVisitor_(const Expr &e, const char *src_tag)
        : node_type(e.defined() ? e->node_type : IRNodeType::IntImm),
          src_tag(src_tag),
          active((ctx.active_bits & BIT_EXPR) && e.defined()) {
        if (active) {
            zone_begin(src_tag, Event::Visitor, (unsigned)node_type);
        }
    }

    ZoneScopedVisitor_(const Stmt &s, const char *src_tag)
        : node_type(s.defined() ? s->node_type : IRNodeType::IntImm),
          src_tag(src_tag),
          active((ctx.active_bits & BIT_STMT) && s.defined()) {
        if (active) {
            zone_begin(src_tag, Event::Visitor, (unsigned)node_type);
        }
    }

    ~ZoneScopedVisitor_() {
        if (active) {
            zone_end(src_tag, Event::Visitor, (unsigned)node_type);
        }
    }
};

struct ZoneScoped_ {
    Event::Tag tag;
    const char *src_tag;

    ZoneScoped_(const char *src_tag, unsigned data = 0, Event::Tag tag = Event::Tag::Generic)
        : tag(tag),
          src_tag(src_tag) {
        zone_begin(src_tag, tag, data);
    }

    ~ZoneScoped_() {
        zone_end(src_tag, tag, 0);
    }
};

#define ZoneScoped \
    Halide::Internal::Profiling::ZoneScoped_ __zone_scoped(__FUNCTION_NAME__)
#define ZoneScopedN(...) \
    Halide::Internal::Profiling::ZoneScoped_ __zone_scoped(__VA_ARGS__)
#define ZoneScopedVisitor(...) \
    Halide::Internal::Profiling::ZoneScopedVisitor_ __zone_scoped(__VA_ARGS__)

#ifdef HALIDE_ENABLE_RTTI
#define HalideVisitorDynamicNameTag typeid(*this).name()
#else
#define HalideVisitorDynamicNameTag __FUNCTION__
#endif

template<typename Base>
class Profiled : public Base {
public:
    using Base::Base;
#ifdef HALIDE_ENABLE_RTTI
    const char *tag = typeid(Base).name();
#else
    const char *tag = "Unknown (no RTTI)";
#endif

#define PROFILE_VISIT_STMT_OVERRIDE(T)                                    \
    auto visit(const T *op) -> decltype(this->Base::visit(op)) override { \
        ZoneScopedVisitor_ _prof(IRNodeType::T, tag, BIT_STMT);           \
        return Base::visit(op);                                           \
    }
    HALIDE_FOR_EACH_IR_STMT(PROFILE_VISIT_STMT_OVERRIDE)

#undef PROFILE_VISIT_STMT_OVERRIDE

#define PROFILE_VISIT_EXPR_OVERRIDE(T)                                    \
    auto visit(const T *op) -> decltype(this->Base::visit(op)) override { \
        ZoneScopedVisitor_ _prof(IRNodeType::T, tag, BIT_EXPR);           \
        return Base::visit(op);                                           \
    }
    HALIDE_FOR_EACH_IR_EXPR(PROFILE_VISIT_EXPR_OVERRIDE)

#undef PROFILE_VISIT_EXPR_OVERRIDE
};

void write_halide_profiling_trace(const std::string &file);

/** Bookkeeping for the simplifier: how many times it was invoked, how
 * many rewrite rules fired, and how many facts it has learned (e.g. via
 * scoped assumptions injected by If/Select conditions) at once. Counters
 * are thread-local, so that multiple pipelines lowering concurrently on
 * different threads don't stomp on each other's stats. */
struct SimplifierStats {
    uint64_t invocations = 0;
    uint64_t rewrites = 0;
    uint64_t live_facts = 0;
    uint64_t peak_facts = 0;
    uint64_t expr_nodes_visited = 0;
    uint64_t stmt_nodes_visited = 0;

    void reset() {
        invocations = 0;
        rewrites = 0;
        expr_nodes_visited = 0;
        stmt_nodes_visited = 0;
        peak_facts = live_facts;
    }
};

inline thread_local SimplifierStats simplifier_stats;

inline void simplify_invoked() {
    simplifier_stats.invocations++;
}

inline void simplify_fact_learned() {
    simplifier_stats.live_facts++;
    simplifier_stats.peak_facts = std::max(simplifier_stats.peak_facts, simplifier_stats.live_facts);
}

inline void simplify_fact_forgotten() {
    simplifier_stats.live_facts--;
}

inline void simplify_stats_reset_pass() {
    simplifier_stats.reset();
}

}  // namespace Profiling

template<typename Base>
using Profiled = Profiling::Profiled<Base>;

}  // namespace Internal
}  // namespace Halide

#else

namespace Profiling {
inline void generic_zone_begin(const char *src_tag, unsigned data = 0) {
}
inline void generic_zone_end(const char *src_tag, unsigned data = 0) {
}
inline void simplify_invoked() {
}
inline void simplify_fact_learned() {
}
inline void simplify_fact_forgotten() {
}
inline void simplify_stats_reset_pass() {
}
}  // namespace Profiling

template<typename Base>
using Profiled = Base;
#define ZoneScoped
#define ZoneScopedN(...)
#define ZoneScopedVisitor(...)
#define HalideVisitorDynamicNameTag

#endif

#endif  // HALIDE_PROFILED_IR_VISITOR_H
