#ifndef HALIDE_PROFILED_IR_VISITOR_H
#define HALIDE_PROFILED_IR_VISITOR_H

#ifdef WITH_COMPILER_PROFILING

#include "IR.h"
#include "PerformanceCounter.h"

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

struct Event {
    const char *src_tag;
    uint32_t timer;
    uint8_t event_type : 1;  // 0 for Start, 1 for Stop

    enum Tag : uint8_t {
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

    ZoneScopedVisitor_(IRNodeType node_type, const char *src_tag, uint8_t activation_bits = 0xff)
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
    Profiling::ZoneScoped_ __zone_scoped(__FUNCTION_NAME__)
#define ZoneScopedN(name) \
    Profiling::ZoneScoped_ __zone_scoped(name)
#define ZoneScopedVisitor(op_type, name) \
    Profiling::ZoneScopedVisitor_ __zone_scoped(op_type, name)

#define VisitorNameTag typeid(*this).name()

template<typename Base>
class Profiled : public Base {
public:
    using Base::Base;
    const char *tag = typeid(Base).name();

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

}  // namespace Profiling

template<typename Base>
using Profiled = Profiling::Profiled<Base>;

}  // namespace Internal
}  // namespace Halide

#else

template<typename Base>
using Profiled = Base;
#define ZoneScoped
#define ZoneScopedN(name)
#define ZoneScopedVisitor(op_type, name)
#define VisitorNameTag

#endif

#endif  // HALIDE_PROFILED_IR_VISITOR_H
