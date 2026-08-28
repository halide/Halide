#include "ScheduleValidator.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

#include "IR.h"
#include "ScheduleAnalyzer.h"

namespace Halide {

namespace {

using Kind = ScheduleDirective::Kind;
using Issue = ScheduleValidator::Issue;
using VarSets = std::vector<std::set<std::string>>;

// Which of a directive's `vars` must already exist (uses), which it creates
// (defines), and which it consumes (kills). `against_pure` checks uses against
// the pure (stage-0) vars instead of the current stage.
struct VarFlow {
    std::vector<size_t> uses, defines, kills;
    bool against_pure = false;
    bool touches_vars = true;
};

// Returns a message if the directive has the wrong number/shape of operands,
// else empty.
std::string operand_error(const ScheduleDirective &d) {
    size_t nv = d.vars.size(), ne = d.exprs.size();
    auto need = [](bool ok, const char *msg) { return ok ? std::string() : std::string(msg); };
    switch (d.kind) {
    case Kind::Split:
        return need(nv == 3 && ne == 1, "split expects 3 vars and 1 factor");
    case Kind::Fuse:
        return need(nv == 3, "fuse expects 3 vars");
    case Kind::Rename:
        return need(nv == 2, "rename expects 2 vars");
    case Kind::Reorder:
        return need(nv >= 1, "reorder expects at least 1 var");
    case Kind::Tile:
        return need(ne >= 1 && nv == 3 * ne, "tile expects 3N vars and N factors");
    case Kind::GpuTile:
        return need((nv == 3 && ne == 1) || (nv == 6 && ne == 2),
                    "gpu_tile expects (3 vars, 1 size) or (6 vars, 2 sizes)");
    case Kind::Gpu:
        return need(nv == 2 || nv == 4 || nv == 6, "gpu expects 2, 4, or 6 vars");
    case Kind::Serial:
    case Kind::Partition:
        return need(nv == 1, "expects 1 var");
    case Kind::Bound:
    case Kind::AlignBounds:
    case Kind::SetEstimate:
        return need(nv == 1 && ne == 2, "expects 1 var and 2 exprs");
    case Kind::AlignExtent:
    case Kind::BoundExtent:
    case Kind::BoundStorage:
        return need(nv == 1 && ne == 1, "expects 1 var and 1 expr");
    case Kind::Prefetch:
        return need(d.ref_funcs.size() == 1 && nv == 2 && ne == 1,
                    "prefetch expects 1 func, 2 vars, and 1 offset");
    case Kind::ComputeAt:
    case Kind::StoreAt:
    case Kind::HoistStorage:
        return need(!d.at_func.empty() && !d.at_var.name.empty(),
                    "expects a target Func and var");
    case Kind::ComputeWith:
        return need(!d.at_func.empty(), "compute_with expects a target Func");
    case Kind::Rfactor:
        return need(!d.produces.empty() && !d.vars.empty(),
                    "rfactor expects preserved pairs and an alias");
    case Kind::In:
    case Kind::CloneIn:
        return need(!d.produces.empty(), "in/clone_in expects an alias name");
    case Kind::RingBuffer:
        return need(ne == 1, "ring_buffer expects 1 extent");
    default:
        return {};
    }
}

// Returns a message if a split/tile factor is a non-positive constant.
std::string factor_error(const ScheduleDirective &d) {
    auto nonpositive = [](const Expr &e) {
        const Internal::IntImm *i = e.as<Internal::IntImm>();
        return i && i->value <= 0;
    };
    if (d.kind == Kind::Split && d.exprs.size() == 1 && nonpositive(d.exprs[0])) {
        return "split factor must be positive";
    }
    if (d.kind == Kind::Tile || d.kind == Kind::GpuTile) {
        for (const Expr &e : d.exprs) {
            if (nonpositive(e)) {
                return "tile factor must be positive";
            }
        }
    }
    return {};
}

VarFlow var_flow(const ScheduleDirective &d) {
    VarFlow vf;
    auto all_uses = [&] {
        for (size_t i = 0; i < d.vars.size(); i++) {
            vf.uses.push_back(i);
        }
    };
    switch (d.kind) {
    case Kind::Split:
        vf.uses = {0};
        vf.kills = {0};
        vf.defines = {1, 2};
        break;
    case Kind::Fuse:
        vf.uses = {0, 1};
        vf.kills = {0, 1};
        vf.defines = {2};
        break;
    case Kind::Rename:
        vf.uses = {0};
        vf.kills = {0};
        vf.defines = {1};
        break;
    case Kind::Tile: {
        size_t n = d.exprs.size();
        for (size_t i = 0; i < n && i < d.vars.size(); i++) {
            vf.uses.push_back(i);
            vf.kills.push_back(i);
        }
        for (size_t i = n; i < d.vars.size(); i++) {
            vf.defines.push_back(i);
        }
        break;
    }
    case Kind::GpuTile: {
        size_t n = d.exprs.size();  // 1 (1D) or 2 (2D)
        for (size_t i = 0; i < n && i < d.vars.size(); i++) {
            vf.uses.push_back(i);
            vf.kills.push_back(i);
        }
        for (size_t i = n; i < d.vars.size(); i++) {
            vf.defines.push_back(i);
        }
        break;
    }
    case Kind::Reorder:
    case Kind::Serial:
    case Kind::Parallel:
    case Kind::Vectorize:
    case Kind::Unroll:
    case Kind::Partition:
    case Kind::NeverPartition:
    case Kind::AlwaysPartition:
    case Kind::Gpu:
    case Kind::GpuBlocks:
    case Kind::GpuThreads:
    case Kind::GpuLanes:
    case Kind::Hexagon:
    case Kind::Host:
    case Kind::SmeStreaming:
    case Kind::Prefetch:
    case Kind::ComputeWith:
        all_uses();
        break;
    case Kind::Bound:
    case Kind::AlignBounds:
    case Kind::AlignExtent:
    case Kind::BoundExtent:
    case Kind::BoundStorage:
    case Kind::SetEstimate:
    case Kind::AlignStorage:
    case Kind::FoldStorage:
    case Kind::ReorderStorage:
        all_uses();
        vf.against_pure = true;
        break;
    case Kind::Rfactor:
        // preserved pairs: even indices are RVars used in the update.
        for (size_t i = 0; i + 1 < d.vars.size(); i += 2) {
            vf.uses.push_back(i);
        }
        break;
    default:
        vf.touches_vars = false;
        break;
    }
    return vf;
}

// The Func vars produced by a structural directive, so later directives that
// schedule the new Func can be checked.
VarSets produced_var_sets(const ScheduleDirective &d, const VarSets *source) {
    std::set<std::string> vars = source ? source->front() : std::set<std::string>{};
    if (d.kind == Kind::Rfactor) {
        for (size_t i = 1; i < d.vars.size(); i += 2) {
            vars.insert(d.vars[i].name);  // preserved RVar -> new pure Var
        }
    }
    return {vars};
}

std::vector<Issue> analyze(const ScheduleDirectives &directives, const std::vector<Func> &funcs) {
    // ScheduleAnalyzer reads the Funcs and gives us their per-stage loop vars;
    // we replay the directives against a mutable copy of that model.
    ScheduleAnalyzer analyzer(funcs);
    ScheduleAnalyzer::VarModel model = analyzer.var_model();

    std::vector<Issue> issues;
    auto add_issue = [&](Issue::Kind kind, size_t i, const std::string &func,
                         const std::string &name, const std::string &message) {
        issues.push_back({kind, i, func, name, message});
    };

    for (size_t i = 0; i < directives.size(); i++) {
        const ScheduleDirective &d = directives[i];
        const std::string base = ScheduleAnalyzer::base_name(d.func);

        if (!d.is_stage_level() && d.stage != 0) {
            add_issue(Issue::Kind::FuncLevelOnUpdate, i, d.func, "",
                      "whole-Func directive placed on update stage " + std::to_string(d.stage));
        }
        if (d.kind == Kind::Rfactor && d.stage == 0) {
            add_issue(Issue::Kind::Malformed, i, d.func, "",
                      "rfactor requires an update stage (stage > 0)");
        }
        if (d.func.empty()) {
            add_issue(Issue::Kind::Malformed, i, d.func, "", "directive has no target Func");
        }
        if (std::string msg = operand_error(d); !msg.empty()) {
            add_issue(Issue::Kind::Malformed, i, d.func, "", msg);
        }
        if (std::string msg = factor_error(d); !msg.empty()) {
            add_issue(Issue::Kind::BadFactor, i, d.func, "", msg);
        }

        auto it = model.find(base);
        if (it == model.end()) {
            add_issue(Issue::Kind::MissingFunc, i, d.func, d.func,
                      "no Func named '" + d.func + "' in the pipeline");
        } else {
            VarSets &stages = it->second;
            if (d.stage < 0 || (size_t)d.stage >= stages.size()) {
                add_issue(Issue::Kind::InvalidStage, i, d.func, "",
                          "stage " + std::to_string(d.stage) + " out of range (" +
                              std::to_string(stages.size()) + " stage(s))");
            } else {
                VarFlow vf = var_flow(d);
                if (vf.touches_vars) {
                    const std::set<std::string> &check =
                        vf.against_pure ? stages[0] : stages[d.stage];
                    std::set<std::string> used;
                    for (size_t idx : vf.uses) {
                        if (idx >= d.vars.size()) {
                            continue;
                        }
                        const std::string &name = d.vars[idx].name;
                        if (!check.count(name)) {
                            add_issue(Issue::Kind::MissingVar, i, d.func, name,
                                      "var '" + name + "' does not exist in " + d.func +
                                          " at this point");
                        }
                        if (!used.insert(name).second) {
                            add_issue(Issue::Kind::DuplicateVar, i, d.func, name,
                                      "var '" + name + "' appears more than once");
                        }
                    }
                    if (!vf.against_pure) {
                        std::set<std::string> &cur = stages[d.stage];
                        for (size_t idx : vf.kills) {
                            if (idx < d.vars.size()) {
                                cur.erase(d.vars[idx].name);
                            }
                        }
                        std::set<std::string> defined;
                        for (size_t idx : vf.defines) {
                            if (idx >= d.vars.size()) {
                                continue;
                            }
                            const std::string &name = d.vars[idx].name;
                            if (!defined.insert(name).second) {
                                add_issue(Issue::Kind::DuplicateVar, i, d.func, name,
                                          "directive introduces var '" + name + "' twice");
                            } else if (cur.count(name)) {
                                add_issue(Issue::Kind::VarCollision, i, d.func, name,
                                          "new var '" + name + "' shadows an existing var in " +
                                              d.func);
                            }
                            cur.insert(name);
                        }
                    }
                }
            }
        }

        // Loop-level target (compute_at / store_at / hoist_storage / compute_with).
        if (!d.at_func.empty()) {
            auto at = model.find(ScheduleAnalyzer::base_name(d.at_func));
            if (at == model.end()) {
                add_issue(Issue::Kind::MissingFunc, i, d.func, d.at_func,
                          "compute/store target '" + d.at_func + "' is not in the pipeline");
            } else if (!d.at_var.name.empty()) {
                bool found = false;
                for (const std::set<std::string> &s : at->second) {
                    found = found || s.count(d.at_var.name);
                }
                if (!found) {
                    add_issue(Issue::Kind::MissingVar, i, d.func, d.at_var.name,
                              "loop level var '" + d.at_var.name + "' does not exist in " +
                                  d.at_func);
                }
            }
        }

        // Other referenced Funcs (eager_inline, stream_loads, prefetch).
        for (const std::string &rf : d.ref_funcs) {
            if (!model.count(ScheduleAnalyzer::base_name(rf))) {
                add_issue(Issue::Kind::MissingFunc, i, d.func, rf,
                          "referenced Func '" + rf + "' is not in the pipeline");
            }
        }

        // Structural directives introduce a new Func for later directives.
        if (d.kind == Kind::Rfactor || d.kind == Kind::In || d.kind == Kind::CloneIn) {
            const VarSets *source = (it != model.end()) ? &it->second : nullptr;
            model[ScheduleAnalyzer::base_name(d.produces)] = produced_var_sets(d, source);
        }
    }
    return issues;
}

}  // namespace

ScheduleValidator::ScheduleValidator(const ScheduleDirectives &directives,
                                     const std::vector<Func> &funcs)
    : issues_(analyze(directives, funcs)) {
}

ScheduleValidator::ScheduleValidator(const ScheduleDirectives &directives, const Pipeline &p)
    : issues_(analyze(directives, p.outputs())) {
}

std::vector<Issue> ScheduleValidator::issues_for(size_t index) const {
    std::vector<Issue> result;
    for (const Issue &issue : issues_) {
        if (issue.directive == index) {
            result.push_back(issue);
        }
    }
    return result;
}

std::vector<std::string> ScheduleValidator::missing_funcs() const {
    std::vector<std::string> result;
    for (const Issue &issue : issues_) {
        if (issue.kind == Issue::Kind::MissingFunc &&
            std::find(result.begin(), result.end(), issue.name) == result.end()) {
            result.push_back(issue.name);
        }
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> ScheduleValidator::missing_vars() const {
    std::vector<std::pair<std::string, std::string>> result;
    for (const Issue &issue : issues_) {
        if (issue.kind == Issue::Kind::MissingVar) {
            std::pair<std::string, std::string> entry{issue.func, issue.name};
            if (std::find(result.begin(), result.end(), entry) == result.end()) {
                result.push_back(entry);
            }
        }
    }
    return result;
}

std::string ScheduleValidator::report() const {
    std::ostringstream os;
    for (const Issue &issue : issues_) {
        os << "directive " << issue.directive << " (" << issue.func << "): " << issue.message
           << "\n";
    }
    return os.str();
}

}  // namespace Halide
