#include "IREquality.h"
#include "IROperator.h"
#include "IRVisitor.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

enum CmpResult { Unknown,
                 Equal,
                 LessThan,
                 GreaterThan };

// A helper class for comparing two pieces of IR with the minimum amount of
// recursion.
template<size_t cache_size>
struct Comparer {

    // Points to any cache in use for comparing Expr graphs. Will be non-null
    // exactly when cache_size > 0
    const IRNode **cache;

    // The compare method below does the actual work, but it needs to call out
    // to a variety of template helper functions to compare specific types. We
    // make the syntax in the giant switch statement in the compare method much
    // simpler if we just give these helper functions access to the state in the
    // compare method: The stack pointers, the currently-considered piece of
    // IR, and the result of the comparison so far.
    const IRNode **stack_end = nullptr, **stack_ptr = nullptr;
    const IRNode *next_a = nullptr, *next_b = nullptr;
    CmpResult result = Equal;

    Comparer(const IRNode **cache)
        : cache(cache) {
    }

    // Compare the given member variable of next_a and next_b. If it's an Expr
    // or Stmt, it's guaranteed to be defined.
    template<typename Node, typename MemberType>
    HALIDE_ALWAYS_INLINE void cmp(MemberType Node::*member_ptr) {
        if (result == Equal) {
            cmp(((const Node *)next_a)->*member_ptr, ((const Node *)next_b)->*member_ptr);
        }
    }

    // The same as above, but with no guarantee.
    template<typename Node, typename MemberType>
    HALIDE_ALWAYS_INLINE void cmp_if_defined(MemberType Node::*member_ptr) {
        if (result == Equal) {
            cmp_if_defined(((const Node *)next_a)->*member_ptr, ((const Node *)next_b)->*member_ptr);
        }
    }

    size_t hash(const IRNode *a, const IRNode *b) {
        uintptr_t pa = (uintptr_t)a;
        uintptr_t pb = (uintptr_t)b;
        uintptr_t h = (((pa * 17) ^ (pb * 13)) >> 4);
        h ^= h >> 8;
        h = h & (cache_size - 1);
        return h;
    }

    // See if we've already processed this pair of IR nodes
    bool cache_contains(const IRNode *a, const IRNode *b) {
        size_t h = hash(a, b);
        const IRNode **c = cache + h * 2;
        return (c[0] == a && c[1] == b);
    }

    // Mark a pair of IR nodes as already processed. We don't do this until
    // we're done processing their children, because there aren't going to be
    // any queries to match a node with one of its children, because nodes can't
    // be their own ancestors. Inserting it into the cache too soon just means
    // it's going to be evicted before we need it.
    void cache_insert(const IRNode *a, const IRNode *b) {
        size_t h = hash(a, b);
        const IRNode **c = cache + h * 2;
        c[0] = a;
        c[1] = b;
    }

    // Compare two known-to-be-defined IR nodes. Well... don't actually compare
    // them because that would be a recursive call. Just push them onto the
    // pending tasks stack.
    void cmp(const IRHandle &a, const IRHandle &b) {
        if (cache_size > 0 && cache_contains(a.get(), b.get())) {
            return;
        }

        if (a.get() == b.get()) {
        } else if (stack_ptr == stack_end) {
            // Out of stack space. Make a recursive call to buy some more stack.
            Comparer<cache_size> sub_comparer(cache);
            result = sub_comparer.compare(a.get(), b.get());
        } else {
            *stack_ptr++ = a.get();
            *stack_ptr++ = b.get();
        }
    }

    // Compare two IR nodes, which may or may not be defined.
    HALIDE_ALWAYS_INLINE
    void cmp_if_defined(const IRHandle &a, const IRHandle &b) {
        if (a.defined() < b.defined()) {
            result = LessThan;
        } else if (a.defined() > b.defined()) {
            result = GreaterThan;
        } else if (a.defined() && b.defined()) {
            cmp(a, b);
        }
    }

    template<typename T>
    void cmp(const std::vector<T> &a, const std::vector<T> &b) {
        if (a.size() < b.size()) {
            result = LessThan;
        } else if (a.size() > b.size()) {
            result = GreaterThan;
        } else {
            for (size_t i = 0; i < a.size() && result == Equal; i++) {
                cmp(a[i], b[i]);
            }
        }
    }

    HALIDE_ALWAYS_INLINE
    void cmp(const Range &a, const Range &b) {
        cmp(a.min, b.min);
        cmp(a.extent, b.extent);
    }

    HALIDE_ALWAYS_INLINE
    void cmp(const ModulusRemainder &a, const ModulusRemainder &b) {
        cmp(a.modulus, b.modulus);
        cmp(a.remainder, b.remainder);
    }

    void cmp(const halide_handle_cplusplus_type *ha,
             const halide_handle_cplusplus_type *hb) {
        if (ha == hb) {
            return;
        } else if (!ha) {
            result = LessThan;
        } else if (!hb) {
            result = GreaterThan;
        } else {
            // They're both non-void handle types with distinct type info
            // structs. We now need to distinguish between different C++
            // pointer types (e.g. char * vs const float *). If would be nice
            // if the structs were unique per C++ type. Then comparing the
            // pointers above would be sufficient.  Unfortunately, different
            // shared libraries in the same process each create a distinct
            // struct for the same type. We therefore have to do a deep
            // comparison of the type info fields.
            cmp(ha->reference_type, hb->reference_type);
            cmp(ha->inner_name.name, hb->inner_name.name);
            cmp(ha->inner_name.cpp_type_type, hb->inner_name.cpp_type_type);
            cmp(ha->namespaces, hb->namespaces);
            cmp(ha->enclosing_types, hb->enclosing_types);
            cmp(ha->cpp_type_modifiers, hb->cpp_type_modifiers);
        }
    }

    HALIDE_ALWAYS_INLINE
    void cmp(const Type &a, const Type &b) {
        uint32_t ta = ((halide_type_t)a).as_u32();
        uint32_t tb = ((halide_type_t)b).as_u32();
        if (ta < tb) {
            result = LessThan;
        } else if (ta > tb) {
            result = GreaterThan;
        } else {
            if (a.handle_type || b.handle_type) {
                cmp(a.handle_type, b.handle_type);
            }
        }
    }

    void cmp(const PrefetchDirective &a, const PrefetchDirective &b) {
        cmp(a.name, b.name);
        cmp(a.at, b.at);
        cmp(a.from, b.from);
        cmp(a.offset, b.offset);
        cmp(a.strategy, b.strategy);
    }

    HALIDE_ALWAYS_INLINE
    void cmp(double a, double b) {
        // Floating point scalars need special handling, due to NaNs.
        if (std::isnan(a) && std::isnan(b)) {
        } else if (std::isnan(a)) {
            result = LessThan;
        } else if (std::isnan(b)) {
            result = GreaterThan;
        } else if (a < b) {
            result = LessThan;
        } else if (b < a) {
            result = GreaterThan;
        }
    }

    HALIDE_ALWAYS_INLINE
    void cmp(const std::string &a, const std::string &b) {
        int r = a.compare(b);
        if (r < 0) {
            result = LessThan;
        } else if (r > 0) {
            result = GreaterThan;
        }
    }

    // The method to use whenever we can just use operator< and get a bool.
    template<typename T, typename = std::enable_if_t<!std::is_convertible_v<T, IRHandle> &&
                                                     std::is_same_v<decltype(std::declval<T>() < std::declval<T>()), bool>>>
    HALIDE_NEVER_INLINE void cmp(const T &a, const T &b) {
        if (a < b) {
            result = LessThan;
        } else if (b < a) {
            result = GreaterThan;
        }
    }

    CmpResult compare(const IRNode *root_a, const IRNode *root_b) {
        constexpr size_t stack_size = 64;             // 1 kb
        const IRNode *stack_storage[stack_size * 2];  // Intentionally uninitialized

        stack_ptr = stack_storage;
        stack_end = stack_storage + stack_size * 2;
        result = Equal;

        *stack_ptr++ = root_a;
        *stack_ptr++ = root_b;

        while (result == Equal && stack_ptr > stack_storage) {
            stack_ptr -= 2;
            next_a = stack_ptr[0];
            next_b = stack_ptr[1];

            if (next_a == next_b) {
                continue;
            }

            if (cache_size > 0 && (((uintptr_t)next_a) & 1)) {
                // If we are using a cache, we want to keep the nodes on the
                // stack while processing their children, but mark them with a
                // tombstone. We'll flip the low bit to 1 for our tombstone. We
                // want to insert them into the cache when the tombstone is
                // handled. This if statement triggers if we just hit a
                // tombstone.
                cache_insert((const IRNode *)((uintptr_t)next_a ^ 1), next_b);
                continue;
            }

            cmp(next_a->node_type, next_b->node_type);
            if (result != Equal) {
                break;
            }

            if (next_a->node_type < IRNodeType::LetStmt) {
                cmp(&BaseExprNode::type);
            }

            if (cache_size > 0) {
                // Keep the parent nodes on the stack, but mark them with a
                // tombstone bit.
                stack_ptr[0] = (const IRNode *)(((uintptr_t)next_a) | 1);
                stack_ptr += 2;
            }

            switch (next_a->node_type) {
            case IRNodeType::IntImm:
                cmp(&IntImm::value);
                break;
            case IRNodeType::UIntImm:
                cmp(&UIntImm::value);
                break;
            case IRNodeType::FloatImm:
                cmp(&FloatImm::value);
                break;
            case IRNodeType::StringImm:
                cmp(&StringImm::value);
                break;
            case IRNodeType::Broadcast:
                cmp(&Broadcast::value);
                break;
            case IRNodeType::Cast:
                cmp(&Cast::value);
                break;
            case IRNodeType::Reinterpret:
                cmp(&Cast::value);
                break;
            case IRNodeType::Variable:
                cmp(&Variable::name);
                break;
            case IRNodeType::Add:
                cmp(&Add::a);
                cmp(&Add::b);
                break;
            case IRNodeType::Sub:
                cmp(&Sub::a);
                cmp(&Sub::b);
                break;
            case IRNodeType::Mod:
                cmp(&Mod::a);
                cmp(&Mod::b);
                break;
            case IRNodeType::Mul:
                cmp(&Mul::a);
                cmp(&Mul::b);
                break;
            case IRNodeType::Div:
                cmp(&Div::a);
                cmp(&Div::b);
                break;
            case IRNodeType::Min:
                cmp(&Min::a);
                cmp(&Min::b);
                break;
            case IRNodeType::Max:
                cmp(&Max::a);
                cmp(&Max::b);
                break;
            case IRNodeType::EQ:
                cmp(&EQ::a);
                cmp(&EQ::b);
                break;
            case IRNodeType::NE:
                cmp(&NE::a);
                cmp(&NE::b);
                break;
            case IRNodeType::LT:
                cmp(&LT::a);
                cmp(&LT::b);
                break;
            case IRNodeType::LE:
                cmp(&LE::a);
                cmp(&LE::b);
                break;
            case IRNodeType::GT:
                cmp(&GT::a);
                cmp(&GT::b);
            case IRNodeType::GE:
                cmp(&GE::a);
                cmp(&GE::b);
                break;
            case IRNodeType::And:
                cmp(&And::a);
                cmp(&And::b);
                break;
            case IRNodeType::Or:
                cmp(&Or::a);
                cmp(&Or::b);
                break;
            case IRNodeType::Not:
                cmp(&Not::a);
                break;
            case IRNodeType::Select:
                cmp(&Select::condition);
                cmp(&Select::true_value);
                cmp(&Select::false_value);
                break;
            case IRNodeType::Load:
                cmp(&Load::name);
                cmp(&Load::alignment);
                cmp(&Load::index);
                cmp(&Load::predicate);
                break;
            case IRNodeType::Ramp:
                cmp(&Ramp::stride);
                cmp(&Ramp::base);
                break;
            case IRNodeType::Call:
                cmp(&Call::name);
                cmp(&Call::call_type);
                cmp(&Call::value_index);
                cmp(&Call::args);
                break;
            case IRNodeType::Let:
                cmp(&Let::name);
                cmp(&Let::value);
                cmp(&Let::body);
                break;
            case IRNodeType::Shuffle:
                cmp(&Shuffle::indices);
                cmp(&Shuffle::vectors);
                break;
            case IRNodeType::VectorReduce:
                cmp(&VectorReduce::op);
                cmp(&VectorReduce::value);
                break;
            case IRNodeType::LetStmt:
                cmp(&LetStmt::name);
                cmp(&LetStmt::value);
                cmp(&LetStmt::body);
                break;
            case IRNodeType::AssertStmt:
                cmp(&AssertStmt::condition);
                cmp(&AssertStmt::message);
                break;
            case IRNodeType::ProducerConsumer:
                cmp(&ProducerConsumer::name);
                cmp(&ProducerConsumer::is_producer);
                cmp(&ProducerConsumer::body);
                break;
            case IRNodeType::For:
                cmp(&For::name);
                cmp(&For::for_type);
                cmp(&For::device_api);
                cmp(&For::partition_policy);
                cmp(&For::min);
                cmp(&For::extent);
                cmp(&For::body);
                break;
            case IRNodeType::Acquire:
                cmp(&Acquire::semaphore);
                cmp(&Acquire::count);
                cmp(&Acquire::body);
                break;
            case IRNodeType::Store:
                cmp(&Store::name);
                cmp(&Store::alignment);
                cmp(&Store::predicate);
                cmp(&Store::value);
                cmp(&Store::index);
                break;
            case IRNodeType::Provide:
                cmp(&Provide::name);
                cmp(&Provide::args);
                cmp(&Provide::values);
                break;
            case IRNodeType::Allocate:
                cmp(&Allocate::name);
                cmp(&Allocate::type);
                cmp(&Allocate::free_function);
                cmp_if_defined(&Allocate::new_expr);
                cmp(&Allocate::condition);
                cmp(&Allocate::extents);
                cmp(&Allocate::body);
                break;
            case IRNodeType::Free:
                cmp(&Free::name);
                break;
            case IRNodeType::Realize:
                cmp(&Realize::name);
                cmp(&Realize::types);
                cmp(&Realize::bounds);
                cmp(&Realize::body);
                cmp(&Realize::condition);
                break;
            case IRNodeType::Block:
                cmp(&Block::first);
                cmp(&Block::rest);
                break;
            case IRNodeType::Fork:
                cmp(&Fork::first);
                cmp(&Fork::rest);
                break;
            case IRNodeType::IfThenElse:
                cmp(&IfThenElse::condition);
                cmp(&IfThenElse::then_case);
                cmp_if_defined(&IfThenElse::else_case);
                break;
            case IRNodeType::Evaluate:
                cmp(&Evaluate::value);
                break;
            case IRNodeType::Prefetch:
                cmp(&Prefetch::name);
                cmp(&Prefetch::types);
                cmp(&Prefetch::prefetch);
                cmp(&Prefetch::bounds);
                cmp(&Prefetch::condition);
                cmp(&Prefetch::body);
                break;
            case IRNodeType::Atomic:
                cmp(&Atomic::producer_name);
                cmp(&Atomic::mutex_name);
                cmp(&Atomic::body);
                break;
            case IRNodeType::HoistedStorage:
                cmp(&HoistedStorage::name);
                cmp(&HoistedStorage::body);
                break;
            }
        }

        // Don't hold onto pointers to this stack frame.
        stack_ptr = stack_end = nullptr;
        return result;
    }
};

template<bool use_cache>
bool ir_equal(const IRHandle &a, const IRHandle &b) {
    // Early out for the most common cases.
    if (a.get() == b.get()) {
        return true;
    } else if (a.defined() != b.defined() ||
               a.node_type() != b.node_type()) {
        return false;
    }
    if (use_cache) {
        const IRNode *cache[256] = {0};
        return Comparer<128>(cache).compare(a.get(), b.get()) == Equal;
    } else {
        return Comparer<0>(nullptr).compare(a.get(), b.get()) == Equal;
    }
}

template<bool use_cache>
bool ir_less_than(const IRHandle &a, const IRHandle &b) {
    // Early out for the most common cases
    if (a.get() == b.get()) {
        return false;
    } else if (!a.defined()) {
        return true;
    } else if (!b.defined()) {
        return false;
    }

    if (use_cache) {
        const IRNode *cache[256] = {0};
        return Comparer<128>(cache).compare(a.get(), b.get()) == LessThan;
    } else {
        return Comparer<0>(nullptr).compare(a.get(), b.get()) == LessThan;
    }
}

}  // namespace

bool equal(const Expr &a, const Expr &b) {
    return ir_equal<false>(a, b);
}

bool equal(const Stmt &a, const Stmt &b) {
    return ir_equal<false>(a, b);
}

bool graph_equal(const Expr &a, const Expr &b) {
    return ir_equal<true>(a, b);
}

bool graph_equal(const Stmt &a, const Stmt &b) {
    return ir_equal<true>(a, b);
}

bool graph_less_than(const Expr &a, const Expr &b) {
    return ir_less_than<true>(a, b);
}

bool graph_less_than(const Stmt &a, const Stmt &b) {
    return ir_less_than<true>(a, b);
}

bool IRDeepCompare::operator()(const Expr &a, const Expr &b) const {
    return ir_less_than<false>(a, b);
}

bool IRDeepCompare::operator()(const Stmt &a, const Stmt &b) const {
    return ir_less_than<false>(a, b);
}

bool IRGraphDeepCompare::operator()(const Expr &a, const Expr &b) const {
    return ir_less_than<true>(a, b);
}

bool IRGraphDeepCompare::operator()(const Stmt &a, const Stmt &b) const {
    return ir_less_than<true>(a, b);
}

// Testing code
namespace {

CmpResult flip_result(CmpResult r) {
    switch (r) {
    case LessThan:
        return GreaterThan;
    case Equal:
        return Equal;
    case GreaterThan:
        return LessThan;
    case Unknown:
        return Unknown;
    }
    return Unknown;
}

void check_equal(const Expr &a, const Expr &b) {
    const IRNode *cache[256] = {0};
    CmpResult r = Comparer<128>(cache).compare(a.get(), b.get());
    internal_assert(r == Equal)
        << "Error in ir_equality_test: " << r
        << " instead of " << Equal
        << " when comparing:\n"
        << a
        << "\nand\n"
        << b << "\n";
}

void check_not_equal(const Expr &a, const Expr &b) {
    const IRNode *cache[256] = {0};
    CmpResult r1 = Comparer<128>(cache).compare(a.get(), b.get());
    CmpResult r2 = Comparer<128>(cache).compare(b.get(), a.get());
    internal_assert(r1 != Equal &&
                    r1 != Unknown &&
                    flip_result(r1) == r2)
        << "Error in ir_equality_test: " << r1
        << " is not the opposite of " << r2
        << " when comparing:\n"
        << a
        << "\nand\n"
        << b << "\n";
}

}  // namespace

void ir_equality_test() {
    Expr x = Variable::make(Int(32), "x");
    check_equal(Ramp::make(x, 4, 3), Ramp::make(x, 4, 3));
    check_not_equal(Ramp::make(x, 2, 3), Ramp::make(x, 4, 3));

    check_equal(x, Variable::make(Int(32), "x"));
    check_not_equal(x, Variable::make(Int(32), "y"));

    // Something that will hang if IREquality has poor computational
    // complexity.
    Expr e1 = x, e2 = x;
    for (int i = 0; i < 100; i++) {
        e1 = e1 * e1 + e1;
        e2 = e2 * e2 + e2;
    }
    check_equal(e1, e2);
    // These are only discovered to be not equal way down the tree:
    e2 = e2 * e2 + e2;
    check_not_equal(e1, e2);

    debug(0) << "ir_equality_test passed\n";
}

}  // namespace Internal
}  // namespace Halide
