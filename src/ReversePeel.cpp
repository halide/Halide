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
#include <unordered_set>
#include <utility>
#include <vector>

namespace Halide {
namespace Internal {

namespace {

constexpr int no_node = -1;

// One node of a chain: a pure IR node with the chain as one of its children
// (or none, for a chain of the constant root) and the remaining children as
// operands, each available where the chain's root is bound.
struct Edge {
    IRNodeType kind = IRNodeType::Add;
    int chain_pos = -1;  // Index of the chain among the node's children.
    Type type;
    std::vector<Expr> operands;  // The other children, in order.
    Expr node;                   // The node this edge was made from, to rebuild Calls.
};

bool is_commutative(IRNodeType kind) {
    switch (kind) {
    case IRNodeType::Add:
    case IRNodeType::Mul:
    case IRNodeType::Min:
    case IRNodeType::Max:
    case IRNodeType::EQ:
    case IRNodeType::NE:
    case IRNodeType::And:
    case IRNodeType::Or:
        return true;
    default:
        return false;
    }
}

bool same_edge(const Edge &a, const Edge &b) {
    if (a.kind != b.kind || a.chain_pos != b.chain_pos || a.type != b.type ||
        a.operands.size() != b.operands.size()) {
        return false;
    }
    if (a.kind == IRNodeType::Call) {
        const Call *ca = a.node.as<Call>(), *cb = b.node.as<Call>();
        if (ca->name != cb->name || ca->call_type != cb->call_type ||
            ca->value_index != cb->value_index || !ca->func.same_as(cb->func)) {
            return false;
        }
    }
    for (size_t i = 0; i < a.operands.size(); i++) {
        if (!equal(a.operands[i], b.operands[i])) {
            return false;
        }
    }
    return true;
}

// Rebuild an edge's node around a chain, with (possibly rewritten) operands.
Expr apply_edge(const Edge &e, const Expr &chain, const std::vector<Expr> &operands) {
    std::vector<Expr> kids;
    kids.reserve(operands.size() + 1);
    int n = (int)operands.size() + (e.chain_pos >= 0 ? 1 : 0);
    for (int i = 0, o = 0; i < n; i++) {
        kids.push_back(i == e.chain_pos ? chain : operands[o++]);
    }
    switch (e.kind) {
    case IRNodeType::Add:
        return Add::make(kids[0], kids[1]);
    case IRNodeType::Sub:
        return Sub::make(kids[0], kids[1]);
    case IRNodeType::Mul:
        return Mul::make(kids[0], kids[1]);
    case IRNodeType::Div:
        return Div::make(kids[0], kids[1]);
    case IRNodeType::Mod:
        return Mod::make(kids[0], kids[1]);
    case IRNodeType::Min:
        return Min::make(kids[0], kids[1]);
    case IRNodeType::Max:
        return Max::make(kids[0], kids[1]);
    case IRNodeType::EQ:
        return EQ::make(kids[0], kids[1]);
    case IRNodeType::NE:
        return NE::make(kids[0], kids[1]);
    case IRNodeType::LT:
        return LT::make(kids[0], kids[1]);
    case IRNodeType::LE:
        return LE::make(kids[0], kids[1]);
    case IRNodeType::GT:
        return GT::make(kids[0], kids[1]);
    case IRNodeType::GE:
        return GE::make(kids[0], kids[1]);
    case IRNodeType::And:
        return And::make(kids[0], kids[1]);
    case IRNodeType::Or:
        return Or::make(kids[0], kids[1]);
    case IRNodeType::Not:
        return Not::make(kids[0]);
    case IRNodeType::Select:
        return Select::make(kids[0], kids[1], kids[2]);
    case IRNodeType::Cast:
        return Cast::make(e.type, kids[0]);
    case IRNodeType::Reinterpret:
        return Reinterpret::make(e.type, kids[0]);
    case IRNodeType::Call:
        return e.node.as<Call>()->with(kids);
    default:
        internal_error << "Not a chain node: " << e.node << "\n";
        return Expr();
    }
}

// The uses of a trie node that lie in one region: the host code, or the body of
// one gpu kernel. A region gets its own let for the node, placed inside the
// region, so that kernels don't gain arguments.
struct RegionUses {
    int region = no_node;  // Scope of the region's loop, or no_node for the host.
    int direct = 0;
    int subtree = 0;
    int live_children = 0;
    int scope = no_node;          // Innermost scope holding the direct uses.
    int subtree_scope = no_node;  // Innermost scope holding the whole subtree.
    bool materialized = false;
    std::string name;
};

// A node of the per-root trie of chains. The root node stands for the bare
// variable; each child applies one more node to its parent.
struct TrieNode {
    int root = no_node;  // Index into Analysis::roots.
    int parent = no_node;
    int first_child = no_node;
    int last_child = no_node;
    int next_sibling = no_node;
    Edge edge;  // Unused on a trie root.
    Type type;  // Of the chain up to here.
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

    // Adds an integer constant: something loops track with an offset rather
    // than in a register of its own, so not worth a let when used once.
    bool is_trailing_constant() const {
        return (edge.kind == IRNodeType::Add || edge.kind == IRNodeType::Sub) &&
               type.is_int_or_uint() && edge.operands.size() == 1 && is_const(edge.operands[0]);
    }
};

// Where new lets can go: the body of a LetStmt, of a Let, of a For (past any
// Acquires that begin it, which LowerParallelTasks expects to follow the For
// directly) or of an IfThenElse branch, or around a For, which is where a value
// invariant in that loop lands when hoisted.
enum class ScopeKind : uint8_t { LetStmt,
                                 Let,
                                 For,
                                 ForOuter,
                                 If };

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
    // A let whose value is a chain of another root binds an alias of that
    // chain's trie node instead of a root of its own, so that its uses and
    // those of other lets with the same value share one trie.
    int alias_root = no_node;
    int alias_trie = no_node;
    // The innermost enclosing region boundary: a kernel's outermost gpu loop.
    int boundary = no_node;
    bool in_gpu_kernel = false;
    // A let between two gpu loops is invisible to the host code that launches
    // the kernel, which evaluates the inner loop extents, so no lets go there
    // and uses there count as host uses.
    bool contains_gpu_loop = false;
    // LowerParallelTasks expects a Fork's branches to be a For or an Acquire,
    // so nothing wraps a For directly under a Fork.
    bool blocked = false;
    int region = no_node;
    std::vector<Injected> injected;  // Lets bound here, outermost first.
};

enum class RootKind : uint8_t { Let,
                                Loop,
                                Free,
                                Constant };

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
    int root;  // no_node for anything but a root or alias variable.
    int trie;  // The trie node an alias stands for, else no_node.
};

bool is_root_type(Type t) {
    return t.is_scalar() && t.bits() > 1 && (t.is_int() || t.is_uint() || t.is_float());
}

struct Analysis {
    std::vector<TrieNode> nodes;
    std::vector<Root> roots;
    std::vector<ScopeNode> scopes;
    std::map<std::string, int> free_roots;
    // Pure expressions with no variables at all chain from here.
    int const_root = no_node;
    int materialized = 0;
    std::unordered_set<std::string> injected_names;
    // Names bound as aliases. Once the chain has a let of its own they are
    // mere renames, and are substituted away.
    std::unordered_set<std::string> alias_names;

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
        nodes.back().type = type;
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

    int add_child(int parent, Edge e) {
        int id = (int)nodes.size();
        nodes.emplace_back();
        TrieNode &n = nodes.back();
        n.root = nodes[parent].root;
        n.parent = parent;
        n.type = e.type;
        n.edge = std::move(e);
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
        return !sn.blocked && !sn.contains_gpu_loop && (sn.kind != ScopeKind::Let || sn.node == r.let);
    }

    // The scope where a let of root r goes when its uses lie within scope s:
    // there, the innermost point holding all of them, unless that is inside
    // a loop the value is invariant in. Then it goes just around the
    // outermost such loop, within the region and the root's binding, so the
    // work is not repeated per iteration, but also not moved above anything
    // else, such as onto the bounds-query path.
    int injection_scope(int s, const Root &r) const {
        int region = scopes[s].region;
        int top = s;
        for (int u = s; u != r.scope; u = scopes[u].parent) {
            internal_assert(u != no_node);
            int p = scopes[u].parent;
            if (p == no_node || scopes[p].region != region) {
                break;
            }
            if (scopes[p].kind == ScopeKind::ForOuter) {
                top = p;
            }
        }
        s = top;
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

    // Skip trailing integer constants: they stay at the use, and the node
    // before them gets the let.
    int without_trailing_constants(int n, int stop) const {
        while (n != stop && nodes[n].is_trailing_constant()) {
            n = nodes[n].parent;
        }
        return n;
    }

    void materialize(Root &r, int n, int region, int scope, int &k) {
        RegionUses *ru = nodes[n].find_region(region);
        internal_assert(ru && !ru->materialized);
        ru->materialized = true;
        ru->name = r.name + ".rp" + std::to_string(k++);
        injected_names.insert(ru->name);
        scopes[scope].injected.push_back(Injected{n, region});
        materialized++;
    }

    // Within one region, a chain prefix used by two or more chains gets its
    // own let, unless it is an unbranched step on the way to one that does;
    // such steps fold into the value of the let below them. A chain used
    // once gets a let too if that moves it out of a loop.
    void decide(Root &r, int n, int region, int &k) {
        for (int c = nodes[n].first_child; c != no_node; c = nodes[c].next_sibling) {
            RegionUses *cr = nodes[c].find_region(region);
            if (!cr || cr->subtree == 0) {
                continue;
            }
            if (cr->subtree >= 2 && (cr->direct >= 1 || cr->live_children >= 2)) {
                materialize(r, c, region, injection_scope(cr->subtree_scope, r), k);
            } else if (cr->subtree == 1 && cr->direct == 1) {
                int m = without_trailing_constants(c, r.trie);
                if (m != r.trie && !nodes[m].find_region(region)->materialized) {
                    int scope = injection_scope(cr->subtree_scope, r);
                    if (crosses_loop(scope, cr->subtree_scope)) {
                        materialize(r, m, region, scope, k);
                    }
                }
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
                if (!fold) {
                    n = without_trailing_constants(n, r.trie);
                    fold = n != r.trie && crosses_loop(r.scope, nodes[n].regions[0].subtree_scope);
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

// A Variable or chain node leaves a pending entry tagged with its node. The
// enclosing node takes it right after visiting that child; anything still
// pending when the next entry arrives, or when the traversal ends, was not
// continued and so is a use of the chain.
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

    void bind(const std::string &name, int root, int trie = no_node) {
        bindings.push(name, Binding{depth, root, trie});
        depth++;
    }

    void unbind(const std::string &name) {
        depth--;
        bindings.pop(name);
    }

    // Bind a name the analysis never saw without moving the depth counter, so
    // that depths keep meaning the same thing in both passes.
    void bind_extra(const std::string &name) {
        bindings.push(name, Binding{depth, no_node, no_node});
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
            set_pending(op, root, b && b->trie != no_node ? b->trie : an.roots[root].trie);
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

    // May e, with pending chain p, be an operand of a node on a chain of this
    // root? It must be pure and available where the root is bound: everything
    // it refers to is bound at or outside the root. That goes by the variables
    // e names, not by the root of e's own chain: an alias variable stands for
    // a chain of some root but is itself bound deeper.
    bool operand_ok(const Expr &e, const Pending &p, int root) {
        return expr_depth(e) <= an.roots[root].depth;
    }

    // Does this node continue a chain through one of its children? The pure
    // nodes of scalar type do, except widening casts (codegen folds those
    // into loads and widening arithmetic) and tag intrinsics (free).
    static bool is_chain_node(const BaseExprNode *node) {
        if (!node->type.is_scalar()) {
            return false;
        }
        switch (node->node_type) {
        case IRNodeType::Cast: {
            const Cast *c = (const Cast *)node;
            return c->type.bits() <= c->value.type().bits();
        }
        case IRNodeType::Call: {
            const Call *c = (const Call *)node;
            return c->is_pure() && !Call::as_tag(Expr(c));
        }
        default:
            return true;
        }
    }

    // Given the children of a node and their pending chains, pick the chain
    // the node continues: the child whose root is bound innermost, provided
    // every other child is an operand for that root. Without any chain child,
    // a node over constants alone continues the constant root. Finishes the
    // chains not continued. Returns the chain's child index (-1 for the
    // constant root), or -2 when the node continues nothing.
    int continue_chain(const BaseExprNode *node, const Expr *kids, const Pending *pend, int n,
                       Edge *edge, int *root) {
        int chain = -1;
        if (is_chain_node(node)) {
            for (int i = 0; i < n; i++) {
                if (pend[i].valid() &&
                    (chain < 0 || an.roots[pend[i].root].depth > an.roots[pend[chain].root].depth)) {
                    chain = i;
                }
            }
            *root = chain >= 0 ? pend[chain].root : an.const_root;
            bool ok = true;
            for (int i = 0; i < n && ok; i++) {
                if (i != chain) {
                    ok = operand_ok(kids[i], pend[i], *root);
                }
            }
            if (ok) {
                for (int i = 0; i < n; i++) {
                    if (i != chain && pend[i].valid()) {
                        finish(pend[i]);
                    }
                }
                edge->kind = node->node_type;
                edge->type = node->type;
                edge->node = Expr(node);
                edge->operands.clear();
                for (int i = 0; i < n; i++) {
                    if (i != chain) {
                        edge->operands.push_back(kids[i]);
                    }
                }
                // Commutative nodes are normalized to have the chain first.
                edge->chain_pos = (chain == 1 && n == 2 && is_commutative(edge->kind)) ? 0 : chain;
                return chain;
            }
        }
        for (int i = 0; i < n; i++) {
            if (pend[i].valid()) {
                finish(pend[i]);
            }
        }
        return -2;
    }

    static bool is_boundary(const For *op) {
        return is_gpu(op->for_type);
    }
};

class Analyze : public IRVisitor, public ChainTracker {
    int gpu_nesting = 0;
    const IRNode *fork_child = nullptr;

    using IRVisitor::visit;

    void visit(const IfThenElse *op) override {
        op->condition.accept(this);
        int saved_scope = cur_scope;
        open_scope(ScopeKind::If, op, no_node, false);
        op->then_case.accept(this);
        cur_scope = saved_scope;
        if (op->else_case.defined()) {
            open_scope(ScopeKind::If, op, no_node, false);
            op->else_case.accept(this);
            cur_scope = saved_scope;
        }
    }

    void visit(const Fork *op) override {
        const IRNode *saved = fork_child;
        fork_child = op->first.get();
        op->first.accept(this);
        if (op->rest.defined()) {
            fork_child = op->rest.get();
            op->rest.accept(this);
        }
        fork_child = saved;
    }

    void visit(const Variable *op) override {
        pending_variable(op);
    }

    void visit_nary(const BaseExprNode *node, const Expr *kids, int n) {
        Pending pend[3];
        std::vector<Pending> many;
        Pending *p = pend;
        if (n > 3) {
            many.resize(n);
            p = many.data();
        }
        for (int i = 0; i < n; i++) {
            kids[i].accept(this);
            p[i] = take(kids[i].get());
        }
        Edge edge;
        int root;
        int chain = continue_chain(node, kids, p, n, &edge, &root);
        if (chain != -2) {
            int parent = chain >= 0 ? p[chain].trie : an.roots[root].trie;
            int child = an.find_child(parent, edge);
            if (child == no_node) {
                child = an.add_child(parent, std::move(edge));
            }
            set_pending(node, root, child);
        }
    }

    template<typename T>
    void visit_binary(const T *op) {
        Expr kids[] = {op->a, op->b};
        visit_nary(op, kids, 2);
    }

    void visit(const Add *op) override {
        visit_binary(op);
    }
    void visit(const Sub *op) override {
        visit_binary(op);
    }
    void visit(const Mul *op) override {
        visit_binary(op);
    }
    void visit(const Div *op) override {
        visit_binary(op);
    }
    void visit(const Mod *op) override {
        visit_binary(op);
    }
    void visit(const Min *op) override {
        visit_binary(op);
    }
    void visit(const Max *op) override {
        visit_binary(op);
    }
    void visit(const EQ *op) override {
        visit_binary(op);
    }
    void visit(const NE *op) override {
        visit_binary(op);
    }
    void visit(const LT *op) override {
        visit_binary(op);
    }
    void visit(const LE *op) override {
        visit_binary(op);
    }
    void visit(const GT *op) override {
        visit_binary(op);
    }
    void visit(const GE *op) override {
        visit_binary(op);
    }
    void visit(const And *op) override {
        visit_binary(op);
    }
    void visit(const Or *op) override {
        visit_binary(op);
    }
    void visit(const Not *op) override {
        visit_nary(op, &op->a, 1);
    }
    void visit(const Cast *op) override {
        visit_nary(op, &op->value, 1);
    }
    void visit(const Reinterpret *op) override {
        visit_nary(op, &op->value, 1);
    }
    void visit(const Select *op) override {
        Expr kids[] = {op->condition, op->true_value, op->false_value};
        visit_nary(op, kids, 3);
    }
    void visit(const Call *op) override {
        visit_nary(op, op->args.data(), (int)op->args.size());
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

    void open_let(const IRNode *node, const std::string &name, const Expr &value, ScopeKind kind) {
        // A value that is a chain of another root makes the name an alias of
        // that chain; the value is one use of the chain like any other.
        Pending pv = take(value.get());
        if (pv.valid()) {
            finish(pv);
        }
        int root = no_node;
        if (pv.valid() && pv.trie != an.roots[pv.root].trie) {
            bind(name, pv.root, pv.trie);
            an.alias_names.insert(name);
        } else {
            if (is_root_type(value.type())) {
                root = an.add_root(RootKind::Let, node, name, value.type(), depth);
            }
            bind(name, root);
        }
        int scope = open_scope(kind, node, root, false);
        if (root != no_node) {
            an.roots[root].scope = scope;
        } else if (pv.valid()) {
            an.scopes[scope].alias_root = pv.root;
            an.scopes[scope].alias_trie = pv.trie;
        }
    }

    void visit(const LetStmt *op) override {
        int saved_scope = cur_scope;
        std::vector<const LetStmt *> frames;
        Stmt body;
        while (op) {
            op->value.accept(this);
            open_let(op, op->name, op->value, ScopeKind::LetStmt);
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
            open_let(op, op->name, op->value, ScopeKind::Let);
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
        int saved_scope = cur_scope;
        int outer = open_scope(ScopeKind::ForOuter, op, no_node, false);
        an.scopes[outer].blocked = op == fork_child;
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
        return &r->name;
    }

    Expr visit_nary(const BaseExprNode *node, const Expr *kids, int n) {
        Pending pend[3];
        Expr mutated_storage[3];
        std::vector<Pending> many_pend;
        std::vector<Expr> many_mutated;
        Pending *p = pend;
        Expr *mutated = mutated_storage;
        if (n > 3) {
            many_pend.resize(n);
            many_mutated.resize(n);
            p = many_pend.data();
            mutated = many_mutated.data();
        }
        bool changed = false;
        for (int i = 0; i < n; i++) {
            mutated[i] = mutate(kids[i]);
            p[i] = take(mutated[i].get());
            changed |= !mutated[i].same_as(kids[i]);
        }
        // Decide and look up with the original children, as the analysis did;
        // build with the rewritten ones.
        Edge edge;
        int root;
        int chain = continue_chain(node, kids, p, n, &edge, &root);
        int child = no_node;
        if (chain != -2) {
            child = an.find_child(chain >= 0 ? p[chain].trie : an.roots[root].trie, edge);
        }
        const std::string *name = nullptr;
        if (child != no_node) {
            name = visible_name(child, an.scopes[cur_scope].region);
        }
        Expr result;
        if (name) {
            result = Variable::make(node->type, *name);
        } else if (!changed) {
            result = Expr(node);
        } else {
            Edge rebuilt;
            rebuilt.kind = node->node_type;
            rebuilt.type = node->type;
            rebuilt.node = Expr(node);
            rebuilt.chain_pos = 0;
            std::vector<Expr> operands(mutated + 1, mutated + n);
            result = apply_edge(rebuilt, mutated[0], operands);
        }
        if (child != no_node) {
            set_pending(result.get(), root, child);
        }
        return result;
    }

    template<typename T>
    Expr visit_binary(const T *op) {
        Expr kids[] = {op->a, op->b};
        return visit_nary(op, kids, 2);
    }

    Expr visit(const Add *op) override {
        return visit_binary(op);
    }
    Expr visit(const Sub *op) override {
        return visit_binary(op);
    }
    Expr visit(const Mul *op) override {
        return visit_binary(op);
    }
    Expr visit(const Div *op) override {
        return visit_binary(op);
    }
    Expr visit(const Mod *op) override {
        return visit_binary(op);
    }
    Expr visit(const Min *op) override {
        return visit_binary(op);
    }
    Expr visit(const Max *op) override {
        return visit_binary(op);
    }
    Expr visit(const EQ *op) override {
        return visit_binary(op);
    }
    Expr visit(const NE *op) override {
        return visit_binary(op);
    }
    Expr visit(const LT *op) override {
        return visit_binary(op);
    }
    Expr visit(const LE *op) override {
        return visit_binary(op);
    }
    Expr visit(const GT *op) override {
        return visit_binary(op);
    }
    Expr visit(const GE *op) override {
        return visit_binary(op);
    }
    Expr visit(const And *op) override {
        return visit_binary(op);
    }
    Expr visit(const Or *op) override {
        return visit_binary(op);
    }
    Expr visit(const Not *op) override {
        return visit_nary(op, &op->a, 1);
    }
    Expr visit(const Cast *op) override {
        return visit_nary(op, &op->value, 1);
    }
    Expr visit(const Reinterpret *op) override {
        return visit_nary(op, &op->value, 1);
    }
    Expr visit(const Select *op) override {
        Expr kids[] = {op->condition, op->true_value, op->false_value};
        return visit_nary(op, kids, 3);
    }
    Expr visit(const Call *op) override {
        return visit_nary(op, op->args.data(), (int)op->args.size());
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
        // The constant root has no variable; a chain reaching it starts from
        // its first node's operands alone.
        Expr e = base;
        if (!e.defined() && !(a == r.trie && r.kind == RootKind::Constant)) {
            e = Variable::make(an.nodes[a].type, *name);
        }
        for (int p : reverse_view(path)) {
            const Edge &edge = an.nodes[p].edge;
            internal_assert(e.defined() || edge.chain_pos < 0)
                << "ReversePeel: chain of " << r.name << " (kind " << (int)r.kind << ") has no base for "
                << edge.node << " with chain at " << edge.chain_pos << ", parent " << an.nodes[p].parent
                << ", root trie " << r.trie << ", region " << region << "\n";
            std::vector<Expr> operands;
            operands.reserve(edge.operands.size());
            for (const Expr &o : edge.operands) {
                operands.push_back(mutate(o));
            }
            e = apply_edge(edge, e, operands);
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
            body = LetOrLetStmt::make(injected_name(injected[i]), std::move(values[i]), std::move(body));
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
            if (an.scopes[scope].alias_trie != no_node) {
                bind(op->name, an.scopes[scope].alias_root, an.scopes[scope].alias_trie);
            } else {
                bind(op->name, root);
            }
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
            // The lets injected at this let's body may refer to its variable,
            // so it stays bound while their values are built.
            unbind_injected(f.scope);
            cur_scope = f.scope;
            result = wrap_injected<LetOrLetStmt, Body>(f.scope, std::move(result));
            unbind(f.op->name);
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
        int saved_scope = cur_scope;
        int outer = enter_scope(op);
        bind_injected(outer);
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
        int scope = enter_scope(op);
        bindings.ref(op->name).root = an.scopes[scope].root;
        bind_injected(scope);
        body = mutate(body);
        unbind_injected(scope);
        cur_scope = scope;
        body = wrap_injected<LetStmt, Stmt>(scope, std::move(body));
        for (size_t i = acquires.size(); i-- > 0;) {
            body = acquires[i]->with(semaphores[i], counts[i], body);
        }
        unbind(op->name);
        Stmt result = op->with(min, max, body);
        unbind_injected(outer);
        cur_scope = outer;
        result = wrap_injected<LetStmt, Stmt>(outer, std::move(result));
        cur_scope = saved_scope;
        return result;
    }

    Stmt visit_branch(const IfThenElse *op, const Stmt &branch) {
        int saved_scope = cur_scope;
        int scope = enter_scope(op);
        bind_injected(scope);
        Stmt result = mutate(branch);
        unbind_injected(scope);
        cur_scope = scope;
        result = wrap_injected<LetStmt, Stmt>(scope, std::move(result));
        cur_scope = saved_scope;
        return result;
    }

    Stmt visit(const IfThenElse *op) override {
        Expr condition = mutate(op->condition);
        Stmt then_case = visit_branch(op, op->then_case);
        Stmt else_case;
        if (op->else_case.defined()) {
            else_case = visit_branch(op, op->else_case);
        }
        return op->with(condition, then_case, else_case);
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

// Cleans up after the rewrite. An injected let can end up without references:
// its uses may have been operands of a chain that folded into a let placed
// further out, where this let is not in scope, so that let rebuilt the
// operand inline instead. An alias let whose chain got a let of its own is a
// mere rename, and is substituted away.
class DropUnreferenced : public IRMutator {
    std::unordered_map<std::string, int> refs;
    const std::unordered_set<std::string> &aliases;
    std::unordered_map<std::string, Expr> renames;

    using IRMutator::visit;

    Expr visit(const Variable *op) override {
        Expr result = op;
        auto r = renames.find(op->name);
        if (r != renames.end()) {
            result = r->second;
        }
        auto it = refs.find(result.as<Variable>()->name);
        if (it != refs.end()) {
            it->second++;
        }
        return result;
    }

    template<typename LetOrLetStmt, typename Body>
    Body visit_let(const LetOrLetStmt *op) {
        std::vector<const LetOrLetStmt *> frames;
        Body body;
        while (op) {
            if (aliases.count(op->name) && op->value.template as<Variable>()) {
                renames[op->name] = mutate(op->value);
            }
            frames.push_back(op);
            body = op->body;
            op = body.template as<LetOrLetStmt>();
        }
        Body result = mutate(body);
        for (const LetOrLetStmt *f : reverse_view(frames)) {
            if (renames.erase(f->name)) {
                continue;
            }
            auto it = refs.find(f->name);
            if (it != refs.end() && it->second == 0) {
                // Dropped, value and all, so nothing it refers to counts.
                continue;
            }
            Expr value = mutate(f->value);
            if (value.same_as(f->value) && result.same_as(f->body)) {
                result = f;
            } else {
                result = LetOrLetStmt::make(f->name, value, result);
            }
        }
        return result;
    }

    Expr visit(const Let *op) override {
        return visit_let<Let, Expr>(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let<LetStmt, Stmt>(op);
    }

public:
    DropUnreferenced(const std::unordered_set<std::string> &injected,
                     const std::unordered_set<std::string> &aliases)
        : aliases(aliases) {
        for (const std::string &n : injected) {
            refs[n] = 0;
        }
        for (const std::string &n : aliases) {
            refs[n] = 0;
        }
    }
};

}  // namespace

Stmt reverse_peel_lets(const Stmt &s) {
    Analysis an;
    an.scopes.emplace_back();  // The top-level scope.
    an.const_root = an.add_root(RootKind::Constant, nullptr, "const", Type(), INT_MIN + 1);
    an.roots[an.const_root].scope = 0;
    Analyze(an).run(s);
    an.resolve();
    debug(2) << "ReversePeel: bound " << an.materialized << " shared chains\n";
    if (an.materialized == 0) {
        return s;
    }
    Stmt result = Rewrite(an).run(s);
    return DropUnreferenced(an.injected_names, an.alias_names)(result);
}

}  // namespace Internal
}  // namespace Halide
