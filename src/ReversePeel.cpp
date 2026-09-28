#include "ReversePeel.h"

#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Scope.h"
#include "Util.h"

#include <algorithm>
#include <climits>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Halide {
namespace Internal {

namespace {

constexpr int no_node = -1;

// Where a shared chain's let goes. Sink puts it at the innermost let or loop
// body holding its uses. Hoist puts it as far out as its operands allow, at the
// top of its region or the root's scope, and also hoists a chain used once when
// that carries it out of a loop.
enum class Placement { Sink,
                       Hoist };

struct Options {
    Placement placement = Placement::Sink;
    // Treat the bodies of parallel loops as regions, like gpu kernels, so that
    // no new value is captured by their closures.
    bool parallel_regions = false;
};

// The operations that can form a chain around a root variable.
enum class ChainOp : uint8_t { Add,
                               Sub,
                               Mul,
                               Div,
                               Mod,
                               Min,
                               Max };

// One operation applied to a chain: which op, which side the chain sits on, and
// the other operand: any pure expression available where the chain's root is
// bound.
struct Edge {
    ChainOp op = ChainOp::Add;
    bool chain_on_left = true;
    Expr operand;
};

bool same_edge(const Edge &a, const Edge &b) {
    return a.op == b.op && a.chain_on_left == b.chain_on_left && equal(a.operand, b.operand);
}

Expr apply_edge(const Edge &e, const Expr &chain, const Expr &operand) {
    Expr a = e.chain_on_left ? chain : operand;
    Expr b = e.chain_on_left ? operand : chain;
    switch (e.op) {
    case ChainOp::Add:
        return Add::make(a, b);
    case ChainOp::Sub:
        return Sub::make(a, b);
    case ChainOp::Mul:
        return Mul::make(a, b);
    case ChainOp::Div:
        return Div::make(a, b);
    case ChainOp::Mod:
        return Mod::make(a, b);
    case ChainOp::Min:
        return Min::make(a, b);
    case ChainOp::Max:
        return Max::make(a, b);
    }
    return Expr();
}

// The uses of a trie node that lie in one region: the host code, or the body of
// one gpu kernel (or parallel loop). A region gets its own let for the node,
// placed inside the region, so that closures don't grow.
struct RegionUses {
    int region = no_node;  // Scope of the region's loop, or no_node for the host.
    int direct = 0;
    int subtree = 0;
    int live_children = 0;
    int scope = no_node;          // Innermost scope holding the direct uses.
    int subtree_scope = no_node;  // Innermost scope holding the whole subtree.
    bool materialized = false;
    // Set by the rewrite when something refers to the let. Uses that were
    // operands of a chain folded into a let further out leave none.
    bool referenced = false;
    std::string name;
};

// A node of the per-root trie of operation chains. The root node stands for
// the bare variable; each child applies one more operation to its parent.
struct TrieNode {
    int root = no_node;  // Index into Analysis::roots.
    int parent = no_node;
    int first_child = no_node;
    int last_child = no_node;
    int next_sibling = no_node;
    Edge edge;  // Unused on a trie root.
    // Direct uses as the analysis finds them: (scope, count), consecutive uses
    // in the same scope merged. Regions are only known once the traversal ends.
    std::vector<std::pair<int, int>> use_scopes;
    // Totals over all regions.
    int direct_uses = 0;
    int subtree_uses = 0;
    int live_children = 0;
    std::vector<RegionUses> regions;
    // A folded node takes over the let of its root, and is visible everywhere.
    bool folded = false;
    std::string folded_name;

    RegionUses *find_region(int region) {
        for (RegionUses &r : regions) {
            if (r.region == region) {
                return &r;
            }
        }
        return nullptr;
    }

    RegionUses &region_uses(int region) {
        if (RegionUses *r = find_region(region)) {
            return *r;
        }
        regions.emplace_back();
        regions.back().region = region;
        return regions.back();
    }

    const std::string *materialized_name(int region) {
        if (folded) {
            return &folded_name;
        }
        for (RegionUses &r : regions) {
            if (r.region == region && r.materialized) {
                r.referenced = true;
                return &r.name;
            }
        }
        return nullptr;
    }
};

// Where new lets can go: the body of a LetStmt, of a Let, or of a For (past any
// Acquires that begin it, which LowerParallelTasks expects to follow the For
// directly).
enum class ScopeKind : uint8_t { LetStmt,
                                 Let,
                                 For };

struct Injected {
    int node;
    int region;
};

struct ScopeNode {
    int parent = no_node;
    int depth = 0;
    ScopeKind kind = ScopeKind::For;
    const IRNode *node = nullptr;
    int root = no_node;  // The root bound by this Let/LetStmt/For, if any.
    // The innermost enclosing region boundary: a kernel's outermost gpu loop,
    // or a parallel loop when those count.
    int boundary = no_node;
    bool in_gpu_kernel = false;
    // A let between two gpu loops is invisible to the host code that launches
    // the kernel, which evaluates the inner loop extents, so no lets go there
    // and uses there count as host uses.
    bool contains_gpu_loop = false;
    int region = no_node;
    std::vector<Injected> injected;  // Lets bound here, outermost first.
};

enum class RootKind : uint8_t { Let,
                                Loop,
                                Free };

struct Root {
    RootKind kind = RootKind::Let;
    const IRNode *let = nullptr;  // The binding Let, LetStmt or For.
    std::string name;
    Type type;
    int scope = no_node;  // The scope node for the binding's body.
    int depth = 0;        // Binding depth; free variables get distinct negative depths.
    int trie = no_node;
    int folded = no_node;  // Trie node whose chain replaces the let's value.
};

struct Binding {
    int depth;
    int root;  // no_node for anything but a root variable.
};

bool is_root_type(Type t) {
    return t.is_scalar() && t.bits() > 1 && (t.is_int() || t.is_uint() || t.is_float());
}

struct Analysis {
    Options options;
    std::vector<TrieNode> nodes;
    std::vector<Root> roots;
    std::vector<ScopeNode> scopes;
    std::map<std::string, int> free_roots;
    int materialized = 0;

    int lca(int a, int b) const {
        if (a == no_node) {
            return b;
        }
        if (b == no_node) {
            return a;
        }
        while (a != b) {
            if (scopes[a].depth > scopes[b].depth) {
                a = scopes[a].parent;
            } else if (scopes[b].depth > scopes[a].depth) {
                b = scopes[b].parent;
            } else {
                a = scopes[a].parent;
                b = scopes[b].parent;
            }
        }
        return a;
    }

    int add_root(RootKind kind, const IRNode *let, const std::string &name, Type type, int depth) {
        int root = (int)roots.size();
        int trie = (int)nodes.size();
        nodes.emplace_back();
        nodes.back().root = root;
        Root r;
        r.kind = kind;
        r.let = let;
        r.name = name;
        r.type = type;
        r.depth = depth;
        r.trie = trie;
        roots.push_back(std::move(r));
        return root;
    }

    int find_child(int parent, const Edge &e) const {
        for (int c = nodes[parent].first_child; c != no_node; c = nodes[c].next_sibling) {
            if (same_edge(nodes[c].edge, e)) {
                return c;
            }
        }
        return no_node;
    }

    int add_child(int parent, const Edge &e) {
        int id = (int)nodes.size();
        nodes.emplace_back();
        TrieNode &n = nodes.back();
        n.root = nodes[parent].root;
        n.parent = parent;
        n.edge = e;
        // Children keep first-seen order, so lets come out in source order.
        TrieNode &p = nodes[parent];
        if (p.last_child == no_node) {
            p.first_child = id;
        } else {
            nodes[p.last_child].next_sibling = id;
        }
        p.last_child = id;
        return id;
    }

    int live_child(int n) const {
        for (int c = nodes[n].first_child; c != no_node; c = nodes[c].next_sibling) {
            if (nodes[c].subtree_uses > 0) {
                return c;
            }
        }
        return no_node;
    }

    void count_subtree(int n) {
        TrieNode &node = nodes[n];
        for (const auto &[scope, count] : node.use_scopes) {
            RegionUses &r = node.region_uses(scopes[scope].region);
            r.direct += count;
            r.subtree += count;
            r.scope = lca(r.scope, scope);
            r.subtree_scope = r.scope;
            node.direct_uses += count;
        }
        node.subtree_uses = node.direct_uses;
        for (int c = node.first_child; c != no_node; c = nodes[c].next_sibling) {
            count_subtree(c);
            const TrieNode &child = nodes[c];
            if (child.subtree_uses == 0) {
                continue;
            }
            node.live_children++;
            node.subtree_uses += child.subtree_uses;
            for (const RegionUses &cr : child.regions) {
                RegionUses &r = node.region_uses(cr.region);
                r.live_children++;
                r.subtree += cr.subtree;
                r.subtree_scope = lca(r.subtree_scope, cr.subtree_scope);
            }
        }
    }

    // Can a let go at scope s for root r? Never between gpu loops, and inside a
    // Let expression only when it is the root's own.
    bool can_inject(int s, const Root &r) const {
        const ScopeNode &sn = scopes[s];
        return !sn.contains_gpu_loop && (sn.kind != ScopeKind::Let || sn.node == r.let);
    }

    // The scope where a let of root r goes when its uses lie within scope s.
    int injection_scope(int s, const Root &r) const {
        if (options.placement == Placement::Hoist) {
            // As far out as the region and the root's binding allow.
            int region = scopes[s].region;
            int top = s;
            for (int u = s; u != r.scope; u = scopes[u].parent) {
                internal_assert(u != no_node);
                int p = scopes[u].parent;
                if (p == no_node || scopes[p].region != region) {
                    break;
                }
                top = p;
            }
            s = top;
        }
        while (s != r.scope && !can_inject(s, r)) {
            internal_assert(s != no_node);
            s = scopes[s].parent;
        }
        return s;
    }

    // Does a let at scope p, used at scope s, sit outside a loop that the use
    // is in?
    bool crosses_loop(int p, int s) const {
        for (int u = s; u != p; u = scopes[u].parent) {
            internal_assert(u != no_node);
            if (scopes[u].kind == ScopeKind::For) {
                return true;
            }
        }
        return false;
    }

    // Within one region, a chain prefix used by two or more chains gets its
    // own let, unless it is an unbranched step on the way to one that does;
    // such steps fold into the value of the let below them. When hoisting, a
    // chain used once gets a let too if that moves it out of a loop.
    void decide(Root &r, int n, int region, int &k) {
        for (int c = nodes[n].first_child; c != no_node; c = nodes[c].next_sibling) {
            RegionUses *cr = nodes[c].find_region(region);
            if (!cr || cr->subtree == 0) {
                continue;
            }
            bool shared = cr->subtree >= 2 && (cr->direct >= 1 || cr->live_children >= 2);
            bool hoisted = false;
            int scope = no_node;
            if (shared || (options.placement == Placement::Hoist && cr->subtree == 1 && cr->direct == 1)) {
                scope = injection_scope(cr->subtree_scope, r);
                hoisted = !shared && crosses_loop(scope, cr->subtree_scope);
            }
            if (shared || hoisted) {
                cr->materialized = true;
                cr->name = r.name + ".rp" + std::to_string(k++);
                scopes[scope].injected.push_back(Injected{c, region});
                materialized++;
            }
            decide(r, c, region, k);
        }
    }

    void resolve() {
        // Scopes are created parents first. A scope between gpu loops belongs
        // to whatever encloses the kernel.
        for (ScopeNode &s : scopes) {
            if (s.contains_gpu_loop) {
                s.region = scopes[s.parent].region;
            } else {
                s.region = s.boundary;
            }
        }
        for (Root &r : roots) {
            count_subtree(r.trie);
            const TrieNode &rt = nodes[r.trie];
            if (rt.subtree_uses == 0) {
                continue;
            }
            int k = 0;
            int top = r.trie;
            if (r.kind == RootKind::Let && rt.direct_uses == 0 && rt.live_children == 1) {
                // Every use goes through one chain. The original let takes
                // that chain into its value and its variable disappears, if
                // the chain is shared or that hoists it out of a loop.
                int n = live_child(r.trie);
                while (nodes[n].direct_uses == 0 && nodes[n].live_children == 1) {
                    n = live_child(n);
                }
                bool fold = rt.subtree_uses >= 2;
                if (!fold && options.placement == Placement::Hoist) {
                    fold = crosses_loop(r.scope, nodes[n].regions[0].subtree_scope);
                }
                if (fold) {
                    nodes[n].folded = true;
                    nodes[n].folded_name = r.name + ".rp" + std::to_string(k++);
                    r.folded = n;
                    materialized++;
                    top = n;
                }
            }
            for (size_t i = 0; i < nodes[top].regions.size(); i++) {
                decide(r, top, nodes[top].regions[i].region, k);
            }
        }
        // A let's value may use chains of roots bound further out, so those
        // lets must come first.
        for (ScopeNode &s : scopes) {
            std::stable_sort(s.injected.begin(), s.injected.end(), [&](const Injected &a, const Injected &b) {
                return roots[nodes[a.node].root].depth < roots[nodes[b.node].root].depth;
            });
        }
    }
};

// A Variable or chain operation leaves a pending entry tagged with its node.
// The enclosing operation takes it right after visiting that operand; anything
// still pending when the next entry arrives, or when the traversal ends, was
// not continued and so is a use of the chain.
struct Pending {
    const IRNode *node = nullptr;
    int root = no_node;
    int trie = no_node;
    int scope = no_node;

    bool valid() const {
        return node != nullptr;
    }
};

// Chain tracking shared by the analysis and the rewrite, so that both see the
// same chains.
class ChainTracker {
protected:
    Analysis &an;
    const bool count_uses;
    Scope<Binding> bindings;
    int depth = 0;
    int cur_scope = 0;
    Pending pending;
    // The innermost binding depth an expression refers to, per node. An
    // expression that can't be moved (a load, an impure call, a let) gets
    // INT_MAX.
    std::unordered_map<const IRNode *, int> depth_cache;

    ChainTracker(Analysis &an, bool count_uses)
        : an(an), count_uses(count_uses) {
    }

    void finish(const Pending &p) {
        if (count_uses) {
            auto &uses = an.nodes[p.trie].use_scopes;
            if (!uses.empty() && uses.back().first == p.scope) {
                uses.back().second++;
            } else {
                uses.emplace_back(p.scope, 1);
            }
        } else {
            internal_assert(an.roots[p.root].folded == no_node || p.trie != an.roots[p.root].trie)
                << "ReversePeel: bare use of " << an.roots[p.root].name << ", whose let was folded\n";
        }
    }

    void flush() {
        if (pending.valid()) {
            finish(pending);
            pending = Pending();
        }
    }

    void set_pending(const IRNode *node, int root, int trie) {
        flush();
        pending = Pending{node, root, trie, cur_scope};
    }

    Pending take(const IRNode *node) {
        Pending p;
        if (pending.node == node) {
            p = pending;
            pending = Pending();
        }
        return p;
    }

    void bind(const std::string &name, int root) {
        bindings.push(name, Binding{depth, root});
        depth++;
    }

    void unbind(const std::string &name) {
        depth--;
        bindings.pop(name);
    }

    // Bind a name the analysis never saw without moving the depth counter, so
    // that depths keep meaning the same thing in both passes.
    void bind_extra(const std::string &name) {
        bindings.push(name, Binding{depth, no_node});
    }

    void unbind_extra(const std::string &name) {
        bindings.pop(name);
    }

    // A free variable of a suitable type is a root bound outside everything.
    // Free variables are ordered by first appearance so that any two of them
    // have one bound "outside" the other.
    int free_root(const Variable *op) {
        auto it = an.free_roots.find(op->name);
        if (it != an.free_roots.end()) {
            return it->second;
        }
        if (!count_uses || !is_root_type(op->type)) {
            return no_node;
        }
        int depth = -(int)an.free_roots.size() - 1;
        int root = an.add_root(RootKind::Free, nullptr, op->name, op->type, depth);
        an.roots[root].scope = 0;
        an.free_roots[op->name] = root;
        return root;
    }

    void pending_variable(const Variable *op) {
        const Binding *b = bindings.find(op->name);
        int root = b ? b->root : free_root(op);
        if (root != no_node) {
            set_pending(op, root, an.roots[root].trie);
        }
    }

    int variable_depth(const Variable *v) const {
        if (const Binding *b = bindings.find(v->name)) {
            return b->depth;
        }
        auto it = an.free_roots.find(v->name);
        return it != an.free_roots.end() ? an.roots[it->second].depth : INT_MIN;
    }

    class ExprDepth : public IRVisitor {
        ChainTracker &tracker;
        using IRVisitor::visit;

        void visit(const Variable *op) override {
            result = std::max(result, tracker.variable_depth(op));
        }
        void visit(const Load *op) override {
            result = INT_MAX;
        }
        void visit(const Let *op) override {
            result = INT_MAX;
        }
        void visit(const Call *op) override {
            if (op->is_pure()) {
                IRVisitor::visit(op);
            } else {
                result = INT_MAX;
            }
        }

    public:
        int result = INT_MIN;
        ExprDepth(ChainTracker &tracker)
            : tracker(tracker) {
        }
    };

    int expr_depth(const Expr &e) {
        if (const Variable *v = e.as<Variable>()) {
            return variable_depth(v);
        }
        if (is_const(e)) {
            return INT_MIN;
        }
        auto it = depth_cache.find(e.get());
        if (it != depth_cache.end()) {
            return it->second;
        }
        ExprDepth d(*this);
        e.accept(&d);
        depth_cache[e.get()] = d.result;
        return d.result;
    }

    // May e, with pending chain p, be the other operand of an operation on a
    // chain of this root? It must be pure and available where the root is
    // bound: everything it refers to is bound at or outside the root.
    bool operand_ok(const Expr &e, const Pending &p, int root) {
        int d = p.valid() ? an.roots[p.root].depth : expr_depth(e);
        return d <= an.roots[root].depth;
    }

    // Given the pending chains of the operands of a binary op, pick the one the
    // op continues, if any, and finish the other. Commutative ops are
    // normalized to have the chain on the left.
    Pending continue_chain(const Pending &pa, const Pending &pb,
                           const Expr &a, const Expr &b,
                           ChainOp op, bool commutative, Edge *edge) {
        if (pa.valid() && operand_ok(b, pb, pa.root)) {
            if (pb.valid()) {
                finish(pb);
            }
            *edge = Edge{op, true, b};
            return pa;
        }
        if (pb.valid() && operand_ok(a, pa, pb.root)) {
            if (pa.valid()) {
                finish(pa);
            }
            *edge = Edge{op, commutative, a};
            return pb;
        }
        if (pa.valid()) {
            finish(pa);
        }
        if (pb.valid()) {
            finish(pb);
        }
        return Pending();
    }

    bool is_boundary(const For *op) const {
        return is_gpu(op->for_type) ||
               (an.options.parallel_regions && op->is_parallel());
    }
};

class Analyze : public IRVisitor, public ChainTracker {
    int gpu_nesting = 0;

    using IRVisitor::visit;

    void visit(const Variable *op) override {
        pending_variable(op);
    }

    template<typename T>
    void visit_binary(const T *op, ChainOp kind, bool commutative) {
        op->a.accept(this);
        Pending pa = take(op->a.get());
        op->b.accept(this);
        Pending pb = take(op->b.get());
        Edge edge;
        Pending chain = continue_chain(pa, pb, op->a, op->b, kind, commutative, &edge);
        if (chain.valid()) {
            int child = an.find_child(chain.trie, edge);
            if (child == no_node) {
                child = an.add_child(chain.trie, edge);
            }
            set_pending(op, chain.root, child);
        }
    }

    void visit(const Add *op) override {
        visit_binary(op, ChainOp::Add, true);
    }
    void visit(const Sub *op) override {
        visit_binary(op, ChainOp::Sub, false);
    }
    void visit(const Mul *op) override {
        visit_binary(op, ChainOp::Mul, true);
    }
    void visit(const Div *op) override {
        visit_binary(op, ChainOp::Div, false);
    }
    void visit(const Mod *op) override {
        visit_binary(op, ChainOp::Mod, false);
    }
    void visit(const Min *op) override {
        visit_binary(op, ChainOp::Min, true);
    }
    void visit(const Max *op) override {
        visit_binary(op, ChainOp::Max, true);
    }

    int open_scope(ScopeKind kind, const IRNode *node, int root, bool boundary) {
        int id = (int)an.scopes.size();
        ScopeNode s;
        s.parent = cur_scope;
        s.depth = an.scopes[cur_scope].depth + 1;
        s.kind = kind;
        s.node = node;
        s.root = root;
        s.boundary = boundary ? id : an.scopes[cur_scope].boundary;
        s.in_gpu_kernel = gpu_nesting > 0;
        an.scopes.push_back(std::move(s));
        cur_scope = id;
        return id;
    }

    void open_let(const IRNode *node, const std::string &name, Type type, ScopeKind kind) {
        int root = no_node;
        if (is_root_type(type)) {
            root = an.add_root(RootKind::Let, node, name, type, depth);
        }
        bind(name, root);
        int scope = open_scope(kind, node, root, false);
        if (root != no_node) {
            an.roots[root].scope = scope;
        }
    }

    void visit(const LetStmt *op) override {
        int saved_scope = cur_scope;
        std::vector<const LetStmt *> frames;
        Stmt body;
        while (op) {
            op->value.accept(this);
            open_let(op, op->name, op->value.type(), ScopeKind::LetStmt);
            frames.push_back(op);
            body = op->body;
            op = body.as<LetStmt>();
        }
        body.accept(this);
        for (const LetStmt *f : reverse_view(frames)) {
            unbind(f->name);
        }
        cur_scope = saved_scope;
    }

    void visit(const Let *op) override {
        int saved_scope = cur_scope;
        std::vector<const Let *> frames;
        Expr body;
        while (op) {
            op->value.accept(this);
            open_let(op, op->name, op->value.type(), ScopeKind::Let);
            frames.push_back(op);
            body = op->body;
            op = body.as<Let>();
        }
        body.accept(this);
        for (const Let *f : reverse_view(frames)) {
            unbind(f->name);
        }
        cur_scope = saved_scope;
    }

    void visit(const For *op) override {
        op->min.accept(this);
        op->max.accept(this);
        bool gpu = is_gpu(op->for_type);
        if (gpu && gpu_nesting > 0) {
            for (int s = cur_scope; an.scopes[s].in_gpu_kernel; s = an.scopes[s].parent) {
                an.scopes[s].contains_gpu_loop = true;
            }
        }
        // The loop variable is a root for the body only; an Acquire count
        // that uses it sits outside the body.
        Type type = op->min.type();
        int root = is_root_type(type) ? an.add_root(RootKind::Loop, op, op->name, type, depth) : no_node;
        bind(op->name, no_node);
        if (gpu) {
            gpu_nesting++;
        }
        Stmt body = op->body;
        while (const Acquire *acq = body.as<Acquire>()) {
            acq->semaphore.accept(this);
            acq->count.accept(this);
            body = acq->body;
        }
        int saved_scope = cur_scope;
        int scope = open_scope(ScopeKind::For, op, root, is_boundary(op));
        if (root != no_node) {
            an.roots[root].scope = scope;
            bindings.ref(op->name).root = root;
        }
        body.accept(this);
        cur_scope = saved_scope;
        if (gpu) {
            gpu_nesting--;
        }
        unbind(op->name);
    }

public:
    Analyze(Analysis &an)
        : ChainTracker(an, true) {
    }

    void run(const Stmt &s) {
        s.accept(this);
        flush();
    }
};

class Rewrite : public IRMutator, public ChainTracker {
    int next_scope = 1;

    using IRMutator::visit;

    // The rewrite walks the IR in the same order as the analysis, so scope
    // nodes come up in creation order.
    int enter_scope(const IRNode *node) {
        int s = next_scope++;
        internal_assert(s < (int)an.scopes.size() && an.scopes[s].node == node)
            << "ReversePeel: analysis and rewrite visited the IR in a different order\n";
        cur_scope = s;
        return s;
    }

    Expr visit(const Variable *op) override {
        pending_variable(op);
        return op;
    }

    template<typename T>
    Expr visit_binary(const T *op, ChainOp kind, bool commutative) {
        Expr a = mutate(op->a);
        Pending pa = take(a.get());
        Expr b = mutate(op->b);
        Pending pb = take(b.get());
        // Decide and look up with the original operands, as the analysis did;
        // build with the rewritten ones.
        Edge edge;
        Pending chain = continue_chain(pa, pb, op->a, op->b, kind, commutative, &edge);
        int child = chain.valid() ? an.find_child(chain.trie, edge) : no_node;
        const std::string *name = nullptr;
        if (child != no_node) {
            name = visible_name(child, an.scopes[cur_scope].region);
        }
        Expr result;
        if (name) {
            result = Variable::make(op->type, *name);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            result = op;
        } else {
            result = T::make(std::move(a), std::move(b));
        }
        if (child != no_node) {
            set_pending(result.get(), chain.root, child);
        }
        return result;
    }

    Expr visit(const Add *op) override {
        return visit_binary(op, ChainOp::Add, true);
    }
    Expr visit(const Sub *op) override {
        return visit_binary(op, ChainOp::Sub, false);
    }
    Expr visit(const Mul *op) override {
        return visit_binary(op, ChainOp::Mul, true);
    }
    Expr visit(const Div *op) override {
        return visit_binary(op, ChainOp::Div, false);
    }
    Expr visit(const Mod *op) override {
        return visit_binary(op, ChainOp::Mod, false);
    }
    Expr visit(const Min *op) override {
        return visit_binary(op, ChainOp::Min, true);
    }
    Expr visit(const Max *op) override {
        return visit_binary(op, ChainOp::Max, true);
    }

    // The name of node n's let for a region, if that let is in scope here. A
    // let of another root may sit deeper than the position being rewritten.
    const std::string *visible_name(int n, int region) {
        TrieNode &node = an.nodes[n];
        if (node.folded) {
            return bindings.contains(node.folded_name) ? &node.folded_name : nullptr;
        }
        RegionUses *r = node.find_region(region);
        if (!r || !r->materialized || !bindings.contains(r->name)) {
            return nullptr;
        }
        r->referenced = true;
        return &r->name;
    }

    // The value of node n's let in a region: its chain applied to the nearest
    // ancestor with a let visible there, or to base when given (the folded
    // root's value). Operands are rewritten here, at the let's own position,
    // so they pick up the lets visible there.
    Expr value_of(int n, int region, const Expr &base = Expr()) {
        const Root &r = an.roots[an.nodes[n].root];
        std::vector<int> path;
        int a = n;
        const std::string *name = nullptr;
        while (true) {
            path.push_back(a);
            a = an.nodes[a].parent;
            if (a == r.trie) {
                name = &r.name;
                break;
            }
            name = visible_name(a, region);
            if (name) {
                break;
            }
        }
        Expr e = base.defined() ? base : Variable::make(r.type, *name);
        for (int p : reverse_view(path)) {
            const Edge &edge = an.nodes[p].edge;
            e = apply_edge(edge, e, mutate(edge.operand));
        }
        return e;
    }

    const std::string &injected_name(const Injected &i) const {
        return an.nodes[i.node].find_region(i.region)->name;
    }

    void bind_injected(int scope) {
        for (const Injected &i : an.scopes[scope].injected) {
            bind_extra(injected_name(i));
        }
    }

    void unbind_injected(int scope) {
        for (const Injected &i : reverse_view(an.scopes[scope].injected)) {
            unbind_extra(injected_name(i));
        }
    }

    // The values of the lets injected at a scope, outermost first. Each is
    // built with the ones before it in scope, so it may use them.
    std::vector<Expr> injected_values(int scope) {
        std::vector<Expr> values;
        for (const Injected &i : an.scopes[scope].injected) {
            values.push_back(value_of(i.node, i.region));
            bind_extra(injected_name(i));
        }
        unbind_injected(scope);
        return values;
    }

    template<typename LetOrLetStmt, typename Body>
    Body wrap_injected(int scope, Body body) {
        std::vector<Expr> values = injected_values(scope);
        const auto &injected = an.scopes[scope].injected;
        for (size_t i = injected.size(); i-- > 0;) {
            // Everything that could refer to this let has been rewritten by now.
            if (an.nodes[injected[i].node].find_region(injected[i].region)->referenced) {
                body = LetOrLetStmt::make(injected_name(injected[i]), std::move(values[i]), std::move(body));
            }
        }
        return body;
    }

    template<typename LetOrLetStmt, typename Body>
    Body visit_let(const LetOrLetStmt *op) {
        struct Frame {
            const LetOrLetStmt *op;
            Expr value;
            int scope;
        };
        int saved_scope = cur_scope;
        std::vector<Frame> frames;
        Body body;
        while (op) {
            Expr value = mutate(op->value);
            int scope = enter_scope(op);
            int root = an.scopes[scope].root;
            bind(op->name, root);
            if (root != no_node && an.roots[root].folded != no_node) {
                bind_extra(an.nodes[an.roots[root].folded].folded_name);
            }
            bind_injected(scope);
            frames.push_back(Frame{op, std::move(value), scope});
            body = op->body;
            op = body.template as<LetOrLetStmt>();
        }
        Body result = mutate(body);
        for (const Frame &f : reverse_view(frames)) {
            unbind_injected(f.scope);
            unbind(f.op->name);
            cur_scope = f.scope;
            result = wrap_injected<LetOrLetStmt, Body>(f.scope, std::move(result));
            int root = an.scopes[f.scope].root;
            if (root != no_node && an.roots[root].folded != no_node) {
                int n = an.roots[root].folded;
                unbind_extra(an.nodes[n].folded_name);
                cur_scope = an.scopes[f.scope].parent;
                result = LetOrLetStmt::make(an.nodes[n].folded_name, value_of(n, no_node, f.value), std::move(result));
            } else if (f.value.same_as(f.op->value) && result.same_as(f.op->body)) {
                result = f.op;
            } else {
                result = LetOrLetStmt::make(f.op->name, f.value, std::move(result));
            }
        }
        cur_scope = saved_scope;
        return result;
    }

    Expr visit(const Let *op) override {
        return visit_let<Let, Expr>(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let<LetStmt, Stmt>(op);
    }

    Stmt visit(const For *op) override {
        Expr min = mutate(op->min);
        Expr max = mutate(op->max);
        bind(op->name, no_node);
        std::vector<const Acquire *> acquires;
        std::vector<Expr> semaphores, counts;
        Stmt body = op->body;
        while (const Acquire *acq = body.as<Acquire>()) {
            acquires.push_back(acq);
            semaphores.push_back(mutate(acq->semaphore));
            counts.push_back(mutate(acq->count));
            body = acq->body;
        }
        int saved_scope = cur_scope;
        int scope = enter_scope(op);
        bindings.ref(op->name).root = an.scopes[scope].root;
        bind_injected(scope);
        body = mutate(body);
        unbind_injected(scope);
        cur_scope = scope;
        body = wrap_injected<LetStmt, Stmt>(scope, std::move(body));
        cur_scope = saved_scope;
        for (size_t i = acquires.size(); i-- > 0;) {
            body = acquires[i]->with(semaphores[i], counts[i], body);
        }
        unbind(op->name);
        return op->with(min, max, body);
    }

    // IRVisitor and IRMutator disagree on the order of the children of these
    // nodes. Follow IRVisitor's order so scopes come up as the analysis saw them.
    Stmt visit(const Allocate *op) override {
        auto [extents, changed] = mutate_with_changes(op->extents);
        Expr condition = mutate(op->condition);
        Expr new_expr;
        if (op->new_expr.defined()) {
            new_expr = mutate(op->new_expr);
        }
        Stmt body = mutate(op->body);
        if (!changed &&
            condition.same_as(op->condition) &&
            new_expr.same_as(op->new_expr) &&
            body.same_as(op->body)) {
            return op;
        }
        return Allocate::make(op->name, op->type, op->memory_type,
                              extents, std::move(condition),
                              std::move(body), std::move(new_expr), op->free_function, op->padding);
    }

    Region mutate_bounds(const Region &bounds) {
        Region result;
        result.reserve(bounds.size());
        for (const Range &r : bounds) {
            Expr min = mutate(r.min);
            Expr extent = mutate(r.extent);
            result.emplace_back(std::move(min), std::move(extent));
        }
        return result;
    }

    Stmt visit(const Realize *op) override {
        Region bounds = mutate_bounds(op->bounds);
        Expr condition = mutate(op->condition);
        Stmt body = mutate(op->body);
        return op->with(bounds, condition, body);
    }

    Stmt visit(const Prefetch *op) override {
        Region bounds = mutate_bounds(op->bounds);
        Expr condition = mutate(op->condition);
        Stmt body = mutate(op->body);
        return op->with(bounds, condition, body);
    }

public:
    Rewrite(Analysis &an)
        : ChainTracker(an, false) {
    }

    Stmt run(const Stmt &s) {
        bind_injected(0);
        Stmt result = mutate(s);
        unbind_injected(0);
        cur_scope = 0;
        return wrap_injected<LetStmt, Stmt>(0, std::move(result));
    }
};

Options options_from_env() {
    Options o;
    std::string policy = get_env_variable("HL_REVERSE_PEEL_POLICY");
    if (policy == "hoist") {
        o.placement = Placement::Hoist;
    } else {
        internal_assert(policy.empty() || policy == "sink") << "Unknown HL_REVERSE_PEEL_POLICY: " << policy << "\n";
    }
    o.parallel_regions = get_env_variable("HL_REVERSE_PEEL_PARALLEL_REGIONS") == "1";
    return o;
}

}  // namespace

Stmt reverse_peel_lets(const Stmt &s) {
    Analysis an;
    an.options = options_from_env();
    an.scopes.emplace_back();  // The top-level scope.
    Analyze(an).run(s);
    an.resolve();
    debug(2) << "ReversePeel: bound " << an.materialized << " shared chains\n";
    if (an.materialized == 0) {
        return s;
    }
    return Rewrite(an).run(s);
}

}  // namespace Internal
}  // namespace Halide
