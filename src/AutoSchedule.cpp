#include<algorithm>

#include "AutoSchedule.h"
#include "RealizationOrder.h"
#include "FindCalls.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Target.h"
#include "Function.h"
#include "Bounds.h"
#include "Var.h"
#include "IRPrinter.h"
#include "Inline.h"
#include "Func.h"
#include "ParallelRVar.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;
using std::map;
using std::set;
using std::deque;
using std::pair;
using std::make_pair;

// Utility functions
int get_extent(const Interval& i) {
    if ((i.min.as<IntImm>()) && (i.max.as<IntImm>())) {
        const IntImm * bmin = i.min.as<IntImm>();
        const IntImm * bmax = i.max.as<IntImm>();
        // Count only if the overlap makes sense
        if (bmin->value <= bmax->value)
            return (bmax->value - bmin->value + 1);
        else
            return 0;
    }
    return -1;
}

int64_t box_area(const Box& b) {
    int64_t box_area = 1;
    for (size_t i = 0; i < b.size(); i++) {
        // Maybe should check for unsigned integers and floats too
        int64_t extent = get_extent(b[i]);
        if (extent > 0 && box_area > 0)
            box_area = box_area * extent;
        else if (extent == 0) {
            box_area = 0;
            break;
        } else {
            box_area = -1;
        }
    }
    return box_area;
}

void disp_regions(map<string, Box>& regions) {
    for (auto& reg: regions) {
        debug(0) << reg.first;
        debug(0) << reg.second;
        debug(0) << "\n";
    }
}

struct FStage {
    Function func;
    uint32_t stage_num;
    FStage(Function _func, uint32_t _stage_num) :
          func(_func), stage_num(_stage_num) { }

    bool operator==(const FStage& other_stage) const {
        return (func.name() == other_stage.func.name()) &&
               (stage_num == other_stage.stage_num);
    }

    bool operator<(const FStage &other_stage) const {
        return func.name() < other_stage.func.name() ||
                (func.name() == other_stage.func.name() &&
                 stage_num < other_stage.stage_num) ;
    }

    friend std::ostream& operator<<(std::ostream& stream, const FStage& s) {
        stream << "(" << s.func.name() << "," << s.stage_num << ")";
        return stream;
    }
};

typedef map<string, Interval> DimBounds;
typedef map<FStage, DimBounds> FStageBounds;

struct MachineParams {
    uint32_t parallelism;
    uint32_t vec_len;
    uint32_t register_file_size;
    uint32_t last_level_size;
    uint32_t balance;
};

Definition get_stage_definition(const Function& f, int stage_num) {

    if (stage_num == 0) {
        return f.definition();
    }
    internal_assert((int)f.updates().size() >= stage_num);
    return f.updates()[stage_num - 1];
}

class FindAllCalls : public IRVisitor {
 public:
  set<string> calls;
  using IRVisitor::visit;

  void visit(const Call *call) {
      // See if images need to be included
      if (call->call_type == Call::Halide ||
          call->call_type == Call::Image) {
          calls.insert(call->name);
      }
      for (size_t i = 0; (i < call->args.size()); i++)
          call->args[i].accept(this);
  }
};

class FindImageInputs : public IRVisitor {
 public:
  map<string, Type> input_type;
  using IRVisitor::visit;

  void visit(const Call *call) {
      if (call->call_type == Call::Image) {
          input_type[call->name] = call->type;
      }
      for (size_t i = 0; (i < call->args.size()); i++)
          call->args[i].accept(this);
  }
};

void set_schedule_defaults(map<string, Function>& env) {
    // Changing the default to compute root.

    // TODO: This ignores existing schedules specified by the user atm and needs
    // to be addressed when we have decided on a mechanism to inform the auto
    // scheduler not to mess with an user specified schedule.
    for (auto& kv : env) {
        // TODO:
        // The shedule is marked touched when a user modifies the schedule. The idea
        // is to keep the user specified schedule intact as much as possible.
        // However, user specified schedules can have non-local effects and will not
        // be captured by touched.  For example: f.compute_at(g, y) now specifies
        // constraints on the schedule of f as well as the schedule of g i.e., the
        // variable y in g cannot be split or reordered since that may change user
        // intent.
        //
        // Open question is how to deal with the constraints induced by user
        // specified schedules.
        kv.second.schedule().store_level() = LoopLevel::root();
        kv.second.schedule().compute_level() = LoopLevel::root();

        // Initializing the schedules for update definitions
        for (size_t u = 0; u < kv.second.updates().size(); u++) {
            kv.second.update_schedule(u).store_level() = LoopLevel::root();
            kv.second.update_schedule(u).compute_level() = LoopLevel::root();
        }
    }
}

bool check_estimates_on_outputs(const vector<Function>& outputs) {
    bool estimates_avail = true;
    for (auto& out : outputs) {
        const vector<Bound>& estimates = out.schedule().estimates();
        if (estimates.size() != out.args().size()) {
            estimates_avail = false;
            break;
        }
        vector<string> vars = out.args();

        for (uint32_t i = 0; i < estimates.size(); i++) {
            if (std::find(vars.begin(), vars.end(), estimates[i].var) == vars.end()
                || !((estimates[i].min.as<IntImm>()) &&
                     (estimates[i].extent.as<IntImm>())))  {
                estimates_avail = false;
                break;
            }
        }
    }
    return estimates_avail;
}

struct CostModel {

    /* Visitor for computing the arithmetic cost of a single value of a function*/
    const map<string, Function> &env;
    map<string, vector<pair<int64_t, int64_t> > > func_cost;
    map<string, Type> inputs;

    class ExprCost : public IRVisitor {
     public:
      int ops;
      int byte_loads;

      ExprCost() {
          ops = 0; byte_loads = 0;
      }

      using IRVisitor::visit;

      void visit(const IntImm *) {}
      void visit(const UIntImm *) {}
      void visit(const FloatImm *) {}
      void visit(const StringImm *) {}
      void visit(const Cast * op) {
          op->value.accept(this);
          ops+=1;
      }
      void visit(const Variable *) {}

      template<typename T>
              void visit_binary_operator(const T *op, int cost) {
                  op->a.accept(this);
                  op->b.accept(this);
                  ops += cost;
              }

      // TODO: Figure out the right costs
      void visit(const Add *op) {visit_binary_operator(op, 1);}
      void visit(const Sub *op) {visit_binary_operator(op, 1);}
      void visit(const Mul *op) {visit_binary_operator(op, 1);}
      void visit(const Div *op) {visit_binary_operator(op, 1);}
      void visit(const Mod *op) {visit_binary_operator(op, 1);}
      void visit(const Min *op) {visit_binary_operator(op, 1);}
      void visit(const Max *op) {visit_binary_operator(op, 1);}
      void visit(const EQ *op) {visit_binary_operator(op, 1);}
      void visit(const NE *op) {visit_binary_operator(op, 1);}
      void visit(const LT *op) {visit_binary_operator(op, 1);}
      void visit(const LE *op) {visit_binary_operator(op, 1);}
      void visit(const GT *op) {visit_binary_operator(op, 1);}
      void visit(const GE *op) {visit_binary_operator(op, 1);}
      void visit(const And *op) {visit_binary_operator(op, 1);}
      void visit(const Or *op) {visit_binary_operator(op, 1);}

      void visit(const Not *op) {
          op->a.accept(this);
          ops+=1;
      }

      void visit(const Select *op) {
          op->condition.accept(this);
          op->true_value.accept(this);
          op->false_value.accept(this);
          ops+=1;
      }

      void visit(const Call * call) {
          if (call->call_type == Call::Halide ||
              call->call_type == Call::Image) {
              ops+=1;
              byte_loads += call->type.bytes();
          } else if (call->call_type == Call::Extern) {
              // There is no visibility into an extern stage so there is
              // no way to know the cost of the call statically. This may
              // require profiling or user annotation
              //
              // For now making this a large constant so that functions with
              // extern stages are forced to be compute_root
              ops+=999;
          } else if (call->call_type == Call::Intrinsic) {
              // TODO: Figure out the right costs based on intrinsic type
              ops+=1;
              // TODO: There is a PureIntrinsic too figure out what it is
              // and how to cost it
          }

          for (size_t i = 0; (i < call->args.size()); i++)
              call->args[i].accept(this);
      }

      void visit(const Let * let) {
          let->value.accept(this);
          let->body.accept(this);
      }

      // Should not hit any of these IR nodes at this stage of compilation
      void visit(const Load *) { internal_assert(0); }
      void visit(const Ramp *) { internal_assert(0); }
      void visit(const Broadcast *) { internal_assert(0); }
      void visit(const LetStmt *) { internal_assert(0); }
      void visit(const AssertStmt *) {}
      void visit(const ProducerConsumer *) { internal_assert(0); }
      void visit(const For *) { internal_assert(0); }
      void visit(const Store *) { internal_assert(0); }
      void visit(const Provide *) { internal_assert(0); }
      void visit(const Allocate *) { internal_assert(0); }
      void visit(const Free *) { internal_assert(0); }
      void visit(const Realize *) { internal_assert(0); }
      void visit(const Block *) { internal_assert(0); }
      void visit(const IfThenElse *) { internal_assert(0); }
      void visit(const Evaluate *) { internal_assert(0); }
    };

    Expr perform_inline(Expr e, const set<string> &inlines = set<string>()) {

        if (inlines.empty())
            return e;

        bool funcs_to_inline = false;
        Expr inlined_expr = e;

        do {
            funcs_to_inline = false;
            FindAllCalls find;
            inlined_expr.accept(&find);
            set<string>& calls = find.calls;
            for (auto& call: calls) {
                if (inlines.find(call) != inlines.end() && env.at(call).is_pure()) {
                    funcs_to_inline = true;
                    inlined_expr = inline_function(inlined_expr, env.at(call));
                    break;
                }
            }
        } while (funcs_to_inline);

        /*
        ExprCost cost;
        e.accept(&cost);
        debug(0) << "Original:" << e << "," << cost.ops << '\n';

        ExprCost cost_inlined;
        inlined_expr.accept(&cost_inlined);
        debug(0) << "Inlined:" << inlined_expr << "," << cost_inlined.ops << '\n';
        */

        return inlined_expr;
    }

    pair<int, int> get_expr_cost(Expr& e) {
        ExprCost cost_visitor;
        e.accept(&cost_visitor);
        return make_pair(cost_visitor.ops, cost_visitor.byte_loads);
    }

    pair<int64_t, int64_t> stage_region_cost(string func, int stage, DimBounds &bounds,
                                             const set<string> &inlines = set<string>()) {
        Function curr_f = env.at(func);
        Definition def = get_stage_definition(curr_f, stage);

        Box stage_region;

        const vector<Dim> &dims = def.schedule().dims();
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            stage_region.push_back(bounds.at(dims[d].var));
        }

        int64_t area = box_area(stage_region);
        if (area < 0) {
            // Area could not be determined therfore it is not possible to
            // determine the cost as well
            return make_pair(-1, -1);
        }

        vector< pair<int64_t, int64_t> > cost = inlines.empty() ? func_cost[func]:
                                                get_func_cost(curr_f, inlines);

        return make_pair(area * cost[stage].first, area * cost[stage].second);
    }


    pair<int64_t, int64_t> stage_region_cost(string func, int stage, Box &region,
                                             const set<string> &inlines = set<string>()) {
        Function curr_f = env.at(func);
        Definition def = get_stage_definition(curr_f, stage);

        // This method of costing update definitions assumes that the domain
        // of the pure vars across all the update definitions is the same
        // which may not be true. This will be prone to overestimating the
        // cost.
        DimBounds bounds;
        const vector<string> &args = curr_f.args();
        for (size_t d = 0; d < args.size(); d++) {
            bounds[args[d]] = region[d];
        }

        for (auto &rvar: def.schedule().rvars()) {
            bounds[rvar.var] = Interval(simplify(rvar.min),
                                        simplify(rvar.min + rvar.extent - 1));
        }

        Box stage_region;

        const vector<Dim> &dims = def.schedule().dims();
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            stage_region.push_back(bounds.at(dims[d].var));
        }

        int64_t area = box_area(stage_region);
        if (area < 0) {
            // Area could not be determined therfore it is not possible to
            // determine the cost as well
            return make_pair(-1, -1);
        }

        vector< pair<int64_t, int64_t> > cost = inlines.empty() ? func_cost[func]:
                                                get_func_cost(curr_f, inlines);

        return make_pair(area * cost[stage].first, area * cost[stage].second);
    }

    pair<int64_t, int64_t> region_cost(string func, Box &region,
                                       const set<string> &inlines = set<string>()) {

        Function curr_f = env.at(func);
        pair<int64_t, int64_t> region_cost;

        int num_stages = curr_f.updates().size() + 1;
        for (int s = 0; s < num_stages; s++) {

            pair<int64_t, int64_t> stage_cost =
                            stage_region_cost(func, s, region, inlines);
            if (stage_cost.first >= 0) {
                region_cost.first += stage_cost.first;
                region_cost.second += stage_cost.second;
            } else {
                return make_pair(-1, -1);
            }
        }

        internal_assert(region_cost.first >= 0 && region_cost.second >=0);
        return region_cost;
    }

    pair<int64_t, int64_t> region_cost(map<string, Box>& regions,
                                       const set<string>& inlines = set<string>()) {

        pair<int64_t, int64_t> total_cost(0, 0);
        for (auto& f: regions) {
            // The cost for inlined functions will be accounted in the consumer
            // of the inlined function
            if (inlines.find(f.first) != inlines.end() &&
                env.at(f.first).is_pure())
                continue;

            pair<int64_t, int64_t> cost = region_cost(f.first, f.second, inlines);
            if (cost.first < 0) {
                return cost;
            }
            else {
                total_cost.first += cost.first;
                total_cost.second += cost.second;
            }
        }

        internal_assert(total_cost.first >= 0 && total_cost.second >=0);
        return total_cost;
    }

    vector< pair<int64_t, int64_t> >
    get_func_cost(const Function &f,
                  const set<string> &inlines = set<string>()) {

        vector< pair<int64_t, int64_t> > func_costs;
        int64_t total_ops = 0;
        int64_t total_bytes = 0;
        // TODO: revist how boundary conditions are handled
        for (auto &e: f.values()) {
            Expr inlined_expr = perform_inline(e, inlines);
            ExprCost cost_visitor;
            inlined_expr.accept(&cost_visitor);
            total_ops += cost_visitor.ops;
            total_bytes += cost_visitor.byte_loads;

            total_bytes += e.type().bytes();
        }

        func_costs.push_back(make_pair(total_ops, total_bytes));

        // Estimating cost when reductions are involved
        if (!f.is_pure()) {
            for (const Definition &u: f.updates()) {
                int64_t ops = 0;
                int64_t bytes = 0;
                for (auto &e: u.values()) {
                    Expr inlined_expr = perform_inline(e, inlines);
                    ExprCost cost_visitor;
                    inlined_expr.accept(&cost_visitor);
                    ops += cost_visitor.ops;
                    bytes += cost_visitor.byte_loads;

                    bytes += e.type().bytes();
                }

                for (auto &arg: u.args()) {
                    Expr inlined_arg = perform_inline(arg, inlines);
                    ExprCost cost_visitor;
                    inlined_arg.accept(&cost_visitor);
                    ops += cost_visitor.ops;
                    bytes += cost_visitor.byte_loads;
                }

                func_costs.push_back(make_pair(ops, bytes));
            }
        }
        return func_costs;
    }

    int64_t get_func_value_size(const Function& f) {
        int64_t size = 0;
        const vector<Type>& types = f.output_types();
        for (size_t i = 0; i < types.size(); i++)
            size += types[i].bytes();
        internal_assert(types.size() != 0);
        return size;
    }

    int64_t region_size(string func, const Box &region) {
        const Function& f = env.at(func);
        int64_t area = box_area(region);
        if (area < 0) {
            // Area could not be determined
            return -1;
        }
        int64_t size_per_ele = get_func_value_size(f);
        return area * size_per_ele;
    }

    int64_t region_size(const map<string, Box>& regions,
                        const set<string>& inlined = set<string>()) {

        map<string, int> num_consumers;
        for (auto& f: regions)
            num_consumers[f.first] = 0;

        for (auto& f: regions) {
            map<string, Function> prods = find_direct_calls(env.at(f.first));
            for (auto& p: prods) {
                if (regions.find(p.first) != regions.end())
                    num_consumers[p.first] += 1;
            }
        }

        vector<Function> outs;
        for (auto &f: num_consumers) {
            if (f.second    == 0) {
                outs.push_back(env.at(f.first));
            }
        }

        // Realization order
        vector<string> order = realization_order(outs, env);

        int64_t working_set_size = 0;
        int64_t curr_size = 0;

        map<string, int64_t> func_sizes;

        for (auto& f: regions) {
            // Inlined functions do not have allocations
            int64_t size = inlined.find(f.first) != inlined.end()? 0:
                    region_size(f.first, f.second);
            if (size < 0)
                return -1;
            else
                func_sizes[f.first] = size;
        }

        for (auto& f: order) {
            if (regions.find(f) != regions.end()) {
                curr_size += func_sizes.at(f);
            }
            working_set_size = std::max(curr_size, working_set_size);
            map<string, Function> prods = find_direct_calls(env.at(f));
            for (auto& p: prods) {
                if (num_consumers.find(p.first) != num_consumers.end()) {
                    num_consumers[p.first] -= 1;
                    if (num_consumers[p.first] == 0) {
                        curr_size -= func_sizes.at(p.first);
                        internal_assert(curr_size >= 0);
                    }
                }
            }
        }

        return working_set_size;
    }

    int64_t input_region_size(string input, const Box &region) {
        int64_t area = box_area(region);
        if (area < 0) {
            // Area could not be determined
            return -1;
        }
        int64_t size_per_ele = inputs.at(input).bytes();
        return area * size_per_ele;
    }

    int64_t input_region_size(const map<string, Box>& input_regions) {
        int64_t total_size = 0;
        for (auto& reg: input_regions) {
            int64_t size = input_region_size(reg.first, reg.second);
            if (size < 0) {
                return -1;
            } else {
                total_size += size;
            }
        }
        return total_size;
    }

    void disp_costs() {
        debug(0) << "===========================" << '\n';
        debug(0) << "Pipeline per element costs:" << '\n';
        debug(0) << "===========================" << '\n';
        for (auto &kv : env) {
            int stage = 0;
            for (auto &cost: func_cost[kv.first]) {
                Definition def = get_stage_definition(kv.second, stage);
                for (auto &e: def.values()) {
                    debug(0) << e << '\n';
                }
                debug(0) << "(" << kv.first << "," << stage << ")" <<
                            "(" << cost.first << "," << cost.second << ")" << '\n';
                stage++;
            }
        }
        debug(0) << "===========================" << '\n';
    }

    CostModel(const map<string, Function>& _env) : env(_env) {
        for (auto& kv : env) {
            func_cost[kv.first] = get_func_cost(kv.second);

            FindImageInputs find;
            kv.second.accept(&find);
            for (auto& in: find.input_type) {
                inputs[in.first] = in.second;
            }
        }
    }
};

struct DependenceAnalysis {

    const map<string, Function>& env;
    const FuncValueBounds& func_val_bounds;

    // TODO: Build a cache for bounds queries

    DependenceAnalysis(map<string, Function>& _env,
                       const FuncValueBounds& _func_val_bounds):
        env(_env), func_val_bounds(_func_val_bounds) {}

    void simplify_box(Box& b) {
        for (size_t i = 0; i < b.size(); i++) {
            b[i].min = simplify(b[i].min);
            b[i].max = simplify(b[i].max);
        }
    }

    map<string, Box> regions_required(Function f, int stage_num,
                                      const DimBounds &bounds,
                                      const set<string> &prods);

    map<string, Box> regions_required(Function f,
                                      const DimBounds &pure_bounds,
                                      const set<string> &prods);

    map<string, Box> redundant_regions(Function f, int stage_num, string var,
                                       const DimBounds &bounds,
                                       const set<string> &prods);
    vector<map<string, Box>>
    overlap_regions(Function f, int stage_num, const DimBounds &bounds,
                    const set<string> &prods);


    DimBounds get_stage_bounds(Function f, int stage_num,
                               const DimBounds &pure_bounds);

    vector<DimBounds> get_stage_bounds(Function f,
                                       const DimBounds &pure_bounds);
};

vector<map<string, Box>>
DependenceAnalysis::overlap_regions(Function f, int stage_num,
                                    const DimBounds &bounds,
                                    const set<string> &prods) {

    vector< map<string, Box> > conc_overlaps;

    Definition def = get_stage_definition(f, stage_num);
    const vector<Dim>& dims = def.schedule().dims();

    for (int d = 0; d < (int)dims.size(); d++) {
        map<string, Box> conc_reg =
                redundant_regions(f, stage_num,
                                  dims[d].var, bounds, prods);
        conc_overlaps.push_back(conc_reg);
    }
    return conc_overlaps;
}

map<string, Box>
DependenceAnalysis::regions_required(Function f,
                                     const DimBounds &pure_bounds,
                                     const set<string> &prods) {
    map<string, Box> regions;
    int num_stages = f.updates().size() + 1;
    for (int s = 0; s < num_stages; s++) {

        DimBounds bounds = get_stage_bounds(f, s, pure_bounds);
        map<string, Box> stage_regions = regions_required(f, s, bounds, prods);

        for (auto& reg: stage_regions) {
            // Merge region with an existing region for the function
            if (regions.find(reg.first) == regions.end())
                regions[reg.first] = reg.second;
            else
                merge_boxes(regions[reg.first], reg.second);
        }
    }
    return regions;
}

map<string, Box>
DependenceAnalysis::regions_required(Function f, int stage_num,
                                     const DimBounds &bounds,
                                     const set<string> &prods) {

    map<string, Box> regions;
    // Add the function and its region to the queue
    deque< pair<FStage, DimBounds> > f_queue;
    FStage start(f, stage_num);
    f_queue.push_back(make_pair(start, bounds));

    // Recursively compute the regions required
    while(!f_queue.empty()) {
        FStage s = f_queue.front().first;
        DimBounds curr_bounds = f_queue.front().second;

        Definition def = get_stage_definition(s.func, s.stage_num);
        Scope<Interval> curr_scope;

        const vector<Dim> &dims = def.schedule().dims();
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            string var_name = dims[d].var;
            internal_assert(curr_bounds.find(var_name) != curr_bounds.end());

            Interval simple_bounds =
                    Interval(simplify(curr_bounds.at(dims[d].var).min),
                             simplify(curr_bounds.at(dims[d].var).max));
            curr_scope.push(var_name, simple_bounds);
        }

        for (auto& val: def.values()) {

            map<string, Box> curr_regions =
                    boxes_required(val, curr_scope, func_val_bounds);

            Box left_reg;
            for (const Expr &arg: def.args()) {
                map<string, Box> arg_regions =
                        boxes_required(arg, curr_scope, func_val_bounds);

                // Merge the regions with the regions found while looking at
                // the values
                for (auto& reg: arg_regions) {
                    if (curr_regions.find(reg.first) == curr_regions.end())
                        curr_regions[reg.first] = reg.second;
                    else
                        merge_boxes(curr_regions[reg.first], reg.second);
                }

                Interval arg_bounds =
                        bounds_of_expr_in_scope(arg, curr_scope, func_val_bounds);
                left_reg.push_back(arg_bounds);
            }

            if (curr_regions.find(s.func.name()) == curr_regions.end()) {
                curr_regions[s.func.name()] = left_reg;
            } else {
                merge_boxes(curr_regions[s.func.name()], left_reg);
            }

            for (auto &reg: curr_regions) {
                // merge region with an existing region for the function in the
                // global map

                if (regions.find(reg.first) == regions.end())
                    regions[reg.first] = reg.second;
                else
                    merge_boxes(regions[reg.first], reg.second);

                // Skip adding to the queue if not the set of producers
                if (prods.find(reg.first) == prods.end())
                    continue;

                if (env.find(reg.first) != env.end() &&
                    reg.first != s.func.name()) {
                    // Add all the stages of the function representing the
                    // region into the queue

                    Function prod_func = env.at(reg.first);
                    DimBounds prod_pure_bounds;
                    const vector<string>& args = prod_func.args();

                    internal_assert(reg.second.size() == args.size());

                    for (size_t v = 0; v < args.size(); v++) {
                        prod_pure_bounds[args[v]] = reg.second[v];
                    }

                    vector<DimBounds> prod_bounds =
                                get_stage_bounds(env.at(reg.first),
                                                 prod_pure_bounds);

                    size_t num_stages = prod_func.updates().size() + 1;

                    internal_assert(prod_bounds.size() == num_stages);

                    for (size_t prod_s = 0; prod_s < num_stages; prod_s++) {
                        FStage prod_stage(prod_func, prod_s);
                        f_queue.push_back(make_pair(prod_stage,
                                                    prod_bounds[prod_s]));
                    }
                }
            }
        }
        f_queue.pop_front();
    }

    // Simplify
    map<string, Box> concrete_regions;

    for (auto& f_reg : regions) {
        simplify_box(f_reg.second);

        Box concrete_box;
        for (uint32_t i = 0; i < f_reg.second.size(); i++) {
            Expr lower = f_reg.second[i].min;
            Expr upper = f_reg.second[i].max;

            // TODO: Assumes estimates cannot be provided on input parameters
            // like images. Need to have a better way of doing this see if
            // input parameters can have estimates attached to them.
            bool in_env = (env.find(f_reg.first) != env.end());

            // Use the estimates if the lower and upper bounds cannot be determined
            if (!lower.as<IntImm>() && in_env) {
                const Function& curr_f = env.at(f_reg.first);
                for (auto& b: curr_f.schedule().estimates()) {
                    uint32_t num_pure_args = curr_f.args().size();
                    if (i < num_pure_args && b.var == curr_f.args()[i])
                        lower = Expr(b.min.as<IntImm>()->value);
                }
            }

            if (!upper.as<IntImm>() && in_env) {
                const Function& curr_f = env.at(f_reg.first);
                for (auto& b: curr_f.schedule().estimates()) {
                    uint32_t num_pure_args = curr_f.args().size();
                    if (i < num_pure_args && b.var == curr_f.args()[i]) {
                        const IntImm * bmin = b.min.as<IntImm>();
                        const IntImm * bextent = b.extent.as<IntImm>();
                        upper = Expr(bmin->value + bextent->value - 1);
                    }
                }
            }

            Interval concrete_bounds = Interval(lower, upper);
            concrete_box.push_back(concrete_bounds);
        }
        concrete_regions[f_reg.first] = concrete_box;
    }
    return concrete_regions;
}

map<string, Box>
DependenceAnalysis::redundant_regions(Function f, int stage_num, string var,
                                      const DimBounds &bounds,
                                      const set<string> &prods) {

    map<string, Box> regions = regions_required(f, stage_num, bounds, prods);

    DimBounds shifted_bounds;

    for (auto &b : bounds) {
        if (b.first == var) {
            Expr len = b.second.max - b.second.min + 1;
            Interval bound = Interval(b.second.min + len,
                                      b.second.max + len);
            shifted_bounds[b.first] = bound;
        }
        else {
            shifted_bounds[b.first] = b.second;
        }
    }

    map<string, Box> regions_shifted = regions_required(f, stage_num,
                                                        shifted_bounds, prods);

    map<string, Box> overalps;
    for (auto &reg: regions) {
        if (regions_shifted.find(reg.first) == regions.end()) {
            // It will be interesting to log cases where this actually happens
            // i.e., the shifted regions do not contain a function that was
            // there in the original regions.
            continue;
        } else {
            Box b = reg.second;
            Box b_shifted = regions_shifted[reg.first];
            // The boxes should be of the same size
            internal_assert(b.size() == b_shifted.size());
            // The box used makes things complicated ignoring it for now
            Box b_intersect;
            for (uint32_t i = 0 ; i < b.size(); i++)
                b_intersect.push_back(interval_intersect(b[i], b_shifted[i]));
            // A function should appear once in the regions and therefore cannot
            // already be present in the overlaps map
            internal_assert(overalps.find(reg.first) == overalps.end());
            overalps[reg.first] = b_intersect;
        }
    }

    // Simplify
    for (auto& f : overalps)
        simplify_box(f.second);

    return overalps;
}

DimBounds
DependenceAnalysis::get_stage_bounds(Function f, int stage_num,
                                     const DimBounds& pure_bounds) {
    DimBounds bounds;
    Definition def = get_stage_definition(f, stage_num);

    // Assumes that the domain of the pure vars across all the update
    // definitions is the same which may not be true. This can overestimate
    // the extent of the domain.
    for (auto& b: pure_bounds) {
        bounds[b.first] = b.second;
    }

    for (auto& rvar: def.schedule().rvars()) {
        Interval simple_bounds = Interval(rvar.min,
                                          simplify(rvar.min + rvar.extent - 1));
        bounds[rvar.var] = simple_bounds;
    }

    return bounds;
}

vector<DimBounds>
DependenceAnalysis::get_stage_bounds(Function f, const DimBounds& pure_bounds) {
    vector<DimBounds> stage_bounds;
    size_t num_stages = f.updates().size() + 1;
    for (size_t s = 0; s < num_stages; s++) {
        stage_bounds.push_back(get_stage_bounds(f, s, pure_bounds));
    }
    return stage_bounds;
}

map<string, Box> get_pipeline_bounds(DependenceAnalysis &analy,
                                     const vector<Function> &outputs) {
    map<string, Box> pipeline_bounds;

    for (auto &out: outputs) {
        DimBounds pure_bounds;
        Box out_box;
        for (auto& arg: out.args()) {
            bool estimate_found = false;
            for (auto& est: out.schedule().estimates()) {
                if (est.var == arg) {
                    Interval I = Interval(est.min, simplify(est.min + est.extent - 1));
                    pure_bounds[arg] = I;
                    out_box.push_back(I);
                    estimate_found = true;
                    break;
                }
            }
            if (!estimate_found) {
                pure_bounds[arg] = Interval();
            }
        }

        set<string> prods;
        for (const pair<string, Function> fpair: analy.env) {
            prods.insert(fpair.first);
        }

        map<string, Box> regions =
                analy.regions_required(out, pure_bounds, prods);

        // Add the output region to the pipeline bounds as well
        regions[out.name()] = out_box;

        for (auto &reg: regions) {
            // Merge region with an existing region for the function in the global map
            if (pipeline_bounds.find(reg.first) == pipeline_bounds.end())
                pipeline_bounds[reg.first] = reg.second;
            else
                merge_boxes(pipeline_bounds[reg.first], reg.second);
        }
    }

    return pipeline_bounds;
}

struct Partitioner {

    struct FusionChoice {
        // FusionChoice encodes the choice of the prod_group being merged with
        // the cons_group at the granularity of the tile given by tile_sizes
        string prod;
        FStage cons;

        FusionChoice(string _prod, FStage _cons) : prod(_prod), cons(_cons) {}

        bool operator==(const FusionChoice& other) const {
            return (prod == other.prod) &&
                    (cons == other.cons);
        }

        bool operator<(const FusionChoice& other) const {
            return prod < other.prod || (prod == other.prod &&
                                         cons < other.cons) ;
        }

        friend std::ostream& operator<<(std::ostream& stream,
                                        const FusionChoice& choice) {

            stream << "Choice:" << choice.prod << "->"
                   << choice.cons << '\n';

            return stream;
        }
    };

    struct EvalConfig {
        map<string, int> tile_sizes;
        int64_t benefit;
        EvalConfig(const map<string, int> &_tile_sizes, int64_t _benefit) :
                   tile_sizes(_tile_sizes), benefit(_benefit) {}
    };

    map<FusionChoice, EvalConfig> fusion_cache;

    struct Group {
        // The output stage representing the group
        FStage output;
        // All the functions that belong to the group
        vector<FStage> members;

        // Reuse along dimensions of the group members
        // TODO: Move this to be a part of group analysis
        map<string, map<string, int64_t> > reuse;

        // Schedule information
        // All the members of the group which are inlined
        set<string> inlined;
        // For now this is just the tile sizes since the we only tile the output of
        // the group and compute all the members of the group at that granularity
        map<string, int> tile_sizes;

        Group(FStage _output, vector<FStage> _members):
              output(_output), members(_members) { }

        friend std::ostream& operator <<(std::ostream& stream, const Group &g) {

            stream << "Output FStage:" << g.output << '\n';
            stream << "Members:" << '[';
            for (auto& m: g.members) {
                stream << m << ",";
            }
            stream << "]" << '\n';

            stream << "Inlined:" << '[';
            for (auto& in: g.inlined) {
                stream << in << ",";
            }
            stream << "]" << '\n';

            stream << "Tile sizes:" << "[";
            for (auto& s: g.tile_sizes) {
                stream << "(" << s.first << "," <<  s.second << ")";
            }
            stream << "]" << '\n';

            return stream;
        }
    };

    struct GroupAnalysis {
        // Estimate of arithmetic cost
        int64_t arith_cost;
        // Estimate of accesses to slow memory
        int64_t mem_cost;
        // Estimate of the parallelism
        int64_t parallelism;

        friend std::ostream& operator <<(std::ostream &stream,
                                         const GroupAnalysis &analy) {
            stream << "[arith cost:" << analy.arith_cost << ",";
            stream << "mem_cost:" << analy.mem_cost << ",";
            stream << "parallelism:" << analy.parallelism << "]\n";

            return stream;
        }
    };

    map<FStage, Group> groups;
    map<FStage, GroupAnalysis> group_costs;

    // Levels that are targetted by the grouping algorithm
    enum Level {INLINE, FAST_MEM};

    map<string, Box>& pipeline_bounds;
    MachineParams& arch_params;
    DependenceAnalysis& dep_analy;
    CostModel& cost_model;
    const vector<Function>& outputs;

    map<FStage, set<FStage> > children;

    bool gpu_schedule;

    Partitioner(map<string, Box>& _pipeline_bounds, MachineParams& _arch_params,
                DependenceAnalysis& _dep_analy, CostModel& _cost_model,
                const vector<Function>& _outputs, bool _gpu_schedule):
        pipeline_bounds(_pipeline_bounds), arch_params(_arch_params),
        dep_analy(_dep_analy), cost_model(_cost_model), outputs(_outputs),
        gpu_schedule(_gpu_schedule) {

            // Place each stage of a function in its own group
            for (auto& f: dep_analy.env) {
                int num_stages = f.second.updates().size() + 1;
                for (int s = 0; s < num_stages; s++) {
                    FStage stg(f.second, s);
                    Group g(stg, {stg});
                    groups.insert(make_pair(stg, g));
                }
            }

            // Find consumers of each function and relate groups with their children
            for (auto& f: dep_analy.env) {
                int num_stages = f.second.updates().size() + 1;
                for (int s = 0; s < num_stages; s++) {

                    FindAllCalls find;
                    Definition def = get_stage_definition(f.second, s);
                    def.accept(&find);

                    for (const string& c: find.calls) {
                        if (c != f.first && dep_analy.env.find(c) != dep_analy.env.end()) {
                            // Consumer depends on the last stage of the producer
                            Function prod_func = dep_analy.env.at(c);
                            int final_stage = prod_func.updates().size();

                            FStage prod_stage(prod_func, final_stage);
                            FStage cons_stage(f.second, s);

                            children[prod_stage].insert(cons_stage);
                        }
                    }

                    if (s > 0) {
                        // Add dependencies between all the stages in a function
                        FStage prod_stage(f.second, s-1);
                        FStage cons_stage(f.second, s);

                        children[prod_stage].insert(cons_stage);
                    }
                }
            }

            // TODO: Any preprocess inlining should go here and they should be added to
            // the corresponding group as inlined members

            // TODO: FindAllCalls might be unnecessary and it probably can be
            // replaced by find_direct_calls
        }

    void merge_groups(const FusionChoice &choice, const EvalConfig &eval,
                      Partitioner::Level level);

    EvalConfig evaluate_choice(const FusionChoice &fuse, Partitioner::Level level);

    Group fuse_groups(Group &g1, Group &g2);

    GroupAnalysis analyze_group(const Group &g);

    map<FStage, map<FStage, DimBounds>> get_group_member_bounds();

    void group(Partitioner::Level level);

    vector<pair<FusionChoice, EvalConfig>>
    choose_candidate_fuse(const vector<pair<string, string>> &cand_pairs,
                          Partitioner::Level level);

    map<string, int64_t> evaluate_reuse(const FStage &stg,
                                        const set<string> &prod);

    map<string, int> bounds_to_estimates(const DimBounds &bounds);

    string generate_cpu_schedule(const Target &t);

    string generate_group_cpu_schedule(const Group &g, const Target &t,
                                       const map<FStage, DimBounds> &group_bounds);

    DimBounds get_bounds(const FStage &stg);

    DimBounds get_bounds_from_tile_sizes(const FStage &stg,
                                         const map<string, int> &tile_sizes);

    vector<map<string, int>> generate_tile_configs(const FStage &stg);

    pair<map<string, int>, GroupAnalysis> find_best_tile_config(const Group &g);

    int64_t estimate_benefit(const GroupAnalysis &nofuse, const GroupAnalysis &fuse,
                             bool no_redundant_work, bool ensure_parallelism);

    int64_t estimate_benefit(const vector<Group> &prod_groups,
                             const Group &cons_group,
                             const GroupAnalysis &fused_analy,
                             bool no_redundant_work, bool ensure_parallelism);

    void initialize_groups_inline();
    void initialize_groups_fast_mem();

    void disp_pipeline_costs();
    void disp_pipeline_bounds();
    void disp_pipeline_graph();
    void disp_grouping();
};

void Partitioner::merge_groups(const FusionChoice &choice, const EvalConfig &eval,
                               Partitioner::Level level) {

    Function prod_f = dep_analy.env.at(choice.prod);
    size_t num_stages = prod_f.updates().size() + 1;

    FStage child = choice.cons;
    Group &child_group = groups.at(child);

    for (size_t s = 0; s < num_stages; s++) {
        FStage cand(prod_f, s);

        internal_assert(groups.find(child) != groups.end());
        Group &cand_group = groups.at(cand);

        vector<FStage> cand_funcs = cand_group.members;

        vector<FStage> &child_group_members = child_group.members;
        child_group_members.insert(child_group_members.end(),
                                   cand_funcs.begin(), cand_funcs.end());

        // TODO: Look at all the members that need to be to be updated. Maybe
        // merge should be a member of the group class so that it is more
        // contained.
        if (level == Partitioner::INLINE) {
            for (auto &stg: cand_funcs) {
                child_group.inlined.insert(stg.func.name());
            }
        } else {
            for (auto &in: cand_group.inlined) {
                child_group.inlined.insert(in);
            }
        }
    }

    child_group.tile_sizes = eval.tile_sizes;

    // Update group costs
    group_costs[child] = analyze_group(child_group);

    /*
    debug(0) << '\n' << groups.at(child_group);
    debug(0) << group_costs[child_group] << '\n';
    */
}

void Partitioner::disp_grouping() {
    debug(0) << "\n=========" << '\n';
    debug(0) << "Grouping:" << '\n';
    debug(0) << "=========" << '\n';
    for (auto& g: groups) {
        debug(0) << g.second << '\n';
    }
    debug(0) << "=========" << '\n';
}

void Partitioner::disp_pipeline_graph() {
    debug(0) << "\n================" << '\n';
    debug(0) << "Pipeline graph:" << '\n';
    debug(0) << "================" << '\n';
    for (auto& f: children) {
        debug(0) << f.first << ": [";
        for (auto& c: f.second) {
            debug(0) << c << ",";
        }
        debug(0) << "]" << '\n';
    }
    debug(0) << "================" << '\n';
}

void Partitioner::disp_pipeline_bounds() {
    debug(0) << "\n================" << '\n';
    debug(0) << "Pipeline bounds:" << '\n';
    debug(0) << "================" << '\n';
    disp_regions(pipeline_bounds);
    debug(0) << "===============" << '\n';
}

void Partitioner::disp_pipeline_costs() {
    if (group_costs.size() == 0) {
        debug(0) << "Group costs have not been analzed yet." << '\n';
        return;
    }
    int64_t total_arith = 0;
    int64_t total_mem = 0;
    debug(0) << "\n===============" << '\n';
    debug(0) << "Pipeline costs:" << '\n';
    debug(0) << "===============" << '\n';
    debug(0) << "Group:(name) [arith cost, mem cost, parallelism]" << '\n';
    for (const pair<FStage, Group> &g: groups) {
        GroupAnalysis analy = group_costs.at(g.first);
        total_mem += analy.mem_cost;
        total_arith += analy.arith_cost;

        debug(0) << "Group:" << g.first << "[";
        debug(0) << analy.arith_cost << "," <<
                 analy.mem_cost << "," << analy.parallelism << "]\n";
    }
    debug(0) << "Total arithmetic cost:" << total_arith << '\n';
    debug(0) << "Total memory cost:" << total_mem << '\n';
    debug(0) << "===============" << '\n';
}

void Partitioner::initialize_groups_inline() {
    for (pair<const FStage, Group> &g: groups) {

        map<string, int> tile_sizes;
        Definition def = get_stage_definition(g.first.func,
                                              g.first.stage_num);

        const vector<Dim> &dims = def.schedule().dims();
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            tile_sizes[dims[d].var] = 1;
        }

        g.second.tile_sizes = tile_sizes;
        GroupAnalysis inline_analy = analyze_group(g.second);
        group_costs[g.second.output] = inline_analy;
    }
    fusion_cache.clear();
}

void Partitioner::initialize_groups_fast_mem() {
    for (pair<const FStage, Group> &g: groups) {
        pair<map<string, int>, GroupAnalysis> best =
            find_best_tile_config(g.second);
        g.second.tile_sizes = best.first;
        group_costs[g.second.output] = best.second;
    }
    fusion_cache.clear();
}

map<string, int64_t> Partitioner::evaluate_reuse(const FStage &stg,
                                                 const set<string> &prod) {
    map<string, int64_t> reuse;
    Function f = stg.func;

    Definition def = get_stage_definition(stg.func, stg.stage_num);

    // TODO: Check if tile sizes of 1 in each dimension gives a reasonable
    // answer or reuse should be evaluated at a much larger granularity or
    // symbolically.  Using a symbolic version might be better if the objective
    // is to prove the dimension has no reuse. The only downside with the
    // symbolic method is it totally at the mercy of the simplifier.  Another
    // option is sampling or using a larger granularity.
    map<string, int> tile_sizes;

    const vector<Dim> &dims = def.schedule().dims();
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        tile_sizes[dims[d].var] = 1;
    }

    DimBounds bounds = get_bounds_from_tile_sizes(stg, tile_sizes);

    set<string> prods;
    vector< map<string, Box> > reuse_regions =
                dep_analy.overlap_regions(stg.func, stg.stage_num, bounds, prods);

    for (int d = 0; d < (int)dims.size() - 1; d++) {
        int64_t total_reuse = 0;
        for (auto &reg: reuse_regions[d]) {
            // Discard all the regions not in procucer set
            if (prod.find(reg.first) == prod.end())
                continue;
            int64_t area = box_area(reg.second);
            if (area >= 0) {
                total_reuse += area;
            } else {
                total_reuse = -1;
                break;
            }
        }
        reuse[dims[d].var] = total_reuse;
    }

    return reuse;
}

vector<pair<Partitioner::FusionChoice, Partitioner::EvalConfig>>
Partitioner::choose_candidate_fuse(const vector<pair<string, string>> &cands,
                                   Partitioner::Level level) {

    vector<pair<FusionChoice, EvalConfig>> best_choices;
    int64_t best_benefit = 0;
    for (auto &p: cands) {
        // Compute the aggregate benefit for inlining into all the children
        int64_t overall_benefit = 0;
        vector<pair<FusionChoice, EvalConfig>> choices;

        Function prod_f = dep_analy.env.at(p.first);
        int final_stage = prod_f.updates().size();

        FStage prod(prod_f.name(), final_stage);

        for (const FStage &c: children[prod]) {

            EvalConfig best_config(map<string, int>(), 0);
            FusionChoice cand_choice(prod_f.name(), c);

            // Check if the pair has been evaluated for inline fusion before
            if (fusion_cache.find(cand_choice) != fusion_cache.end()) {
                // debug(0) << "Cache Hit:" << cand_choice << '\n';
                best_config = fusion_cache.at(cand_choice);

            } else {
                // debug(0) << "Cache Miss:" << cand_choice << '\n';
                best_config = evaluate_choice(cand_choice, level);
                // Cache the result of the evaluation for the pair
                fusion_cache.insert(make_pair(cand_choice, best_config));
            }

            // Conservative strategy that only goes ahead with the fusion if all the
            // fusions into the consumers are beneficial
            // TODO: Create a test where this assumption breaks
            if (best_config.benefit < 0) {
                choices.push_back(make_pair(cand_choice, best_config));
                overall_benefit = -1;
                break;
            } else {
                choices.push_back(make_pair(cand_choice, best_config));
                overall_benefit += best_config.benefit;
            }
        }

        for (auto &choice: choices) {
            debug(0) << "Cand choice:" << choice.first;
        }
        debug(0) << "Cand benefit:" << overall_benefit << '\n';
        // TODO: The grouping process can be non-deterministic when the costs
        // of two choices are equal
        if (best_benefit < overall_benefit
            /*||
              (best.second == overall_benefit &&
              best.first[0].prod_group > p.first)*/) {
            best_choices = choices;
            best_benefit = overall_benefit;
        }
    }

    for (auto &choice: best_choices) {
        debug(0) << "Best choice:" << choice.first;
    }
    if (best_choices.size() > 0)
        debug(0) << "Benefit:" << best_benefit << '\n';

    return best_choices;
}

vector<map<string, int>>
Partitioner::generate_tile_configs(const FStage &stg) {

    int min_vec_dim_size = 8;

    Definition def = get_stage_definition(stg.func, stg.stage_num);
    const vector<Dim> &dims = def.schedule().dims();

    set<string> pure_vars;
    for (const string& arg: stg.func.args()) {
        pure_vars.insert(arg);
    }

    // Get the dimensions that are going to be tiled in this stage.
    // skipping rvars for now.
    vector<string> tile_vars;
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        if (pure_vars.find(dims[d].var) != pure_vars.end()) {
            tile_vars.push_back(dims[d].var);
        }
    }

    vector<int> size_variants = {1, 4, 8, 16, 32, 64, 128, 256};
    vector<map<string, int>> tile_configs;

    // Skewed tile configurations
    for (size_t i = 0; i < tile_vars.size(); i++) {
        for (auto &dim_size: size_variants) {
            map<string, int> tiling;
            for (size_t j = 0; j < tile_vars.size(); j++) {
                if (j == i) {
                    tiling[tile_vars[j]] = j == 0 ?
                                std::max(dim_size, min_vec_dim_size): dim_size;
                } else if (j < i) {
                    tiling[tile_vars[j]] =
                            size_variants[size_variants.size() - 1];
                } else {
                    tiling[tile_vars[j]] = size_variants[0];
                }
            }
            tile_configs.push_back(tiling);
        }
    }

    // Almost square tile configurations
    for (auto &dim_size: size_variants) {
        map<string, int> tiling;
        for (size_t j = 0; j < tile_vars.size(); j++) {
            tiling[tile_vars[j]] = j == 0 ?
                            std::max(dim_size, min_vec_dim_size): dim_size;
        }
        tile_configs.push_back(tiling);
    }

    // Reorder tile configurations
    for (int i = 1; i < (1 << (tile_vars.size() - 1)); i++) {
        map<string, int> tiling;
        for (size_t j = 1; j < tile_vars.size(); j++) {
            if (((i >> (j-1)) & 1) == 1) {
                tiling[tile_vars[j]] = 1;
            }
        }
        tile_configs.push_back(tiling);
    }

    return tile_configs;
}

pair<map<string, int>, Partitioner::GroupAnalysis>
Partitioner::find_best_tile_config(const Group &g) {

    // TODO: Add sanity checks for the cost model
    debug(0) << "\n============\n";
    debug(0) << "Search start\n";
    debug(0) << "============\n";


    // Initialize to no tiling
    map<string, int> no_tile_config;
    Group no_tile = g;
    no_tile.tile_sizes = no_tile_config;

    GroupAnalysis best_analy = analyze_group(no_tile);
    map<string, int> best_config = no_tile_config;

    if (best_analy.arith_cost < 0) {
        return make_pair(best_config, best_analy);
    }

    // Generate tiling configurations
    vector<map<string, int>> configs = generate_tile_configs(g.output);

    for (auto &config: configs) {
        Group new_group = g;
        new_group.tile_sizes = config;

        GroupAnalysis new_analy = analyze_group(new_group);

        int64_t benefit = estimate_benefit(best_analy, new_analy, false, true);
        if (benefit > 0) {
            best_config = config;
            best_analy = new_analy;
            debug(0) << "Relative to current best:" << '\n';
            debug(0) << "Benefit:" << benefit << '\n';
            debug(0) << "arith cost:" << (float)new_analy.arith_cost/best_analy.arith_cost << " ";
            debug(0) << "mem cost:" << (float)new_analy.mem_cost/best_analy.mem_cost << "\n\n";
        }
    }

    debug(0) << "\n===========\n";
    debug(0) << "Best config\n";
    for (auto &dim: best_config) {
        debug(0) << dim.first << ":" << dim.second << " ";
    }
    debug(0) << '\n' << best_analy;

    return make_pair(best_config, best_analy);
}

void Partitioner::group(Partitioner::Level level) {
    // Partition the pipeline by iteratively merging groups until a fixpoint
    bool fixpoint = false;
    while(!fixpoint) {
        fixpoint = true;
        vector<pair<string, string>> cand;
        for (const pair<FStage, Group> &g: groups) {

            bool is_output = false;
            for (const Function &f: outputs) {
                if (g.first.func.name() == f.name()) {
                    is_output = true;
                    break;
                }
            }

            // All the stages of a function are computed at a single location.
            // The last stage of the pipeline represents the candidate choice
            // of fusing the funtion into a consumer.

            const Function &prod_f = dep_analy.env.at(g.first.func.name());
            bool is_final_stage = (g.first.stage_num == prod_f.updates().size());

            if (is_output || !is_final_stage)
                continue;

            if (children.find(g.first) != children.end()) {
                // All the stages beloning to a function are considered to be a
                // single child.
                set<string> child_funcs;
                for (const FStage &s: children[g.first]) {
                    child_funcs.insert(s.func.name());
                }

                int num_children = child_funcs.size();
                // Only groups with a single child are considered for fusion
                // when grouping for computing in tiles. This is because the
                // scheduling model does not allow functions to be computed at
                // different points.
                if (num_children == 1 && level == Partitioner::FAST_MEM) {
                    string prod_name = prod_f.name();
                    string cons_name = (*child_funcs.begin());
                    cand.push_back(make_pair(prod_name, cons_name));
                } else if(num_children > 0  && level == Partitioner::INLINE) {
                    string prod_name = prod_f.name();
                    cand.push_back(make_pair(prod_name, ""));
                }
            }
        }

        debug(0) << "\nCurrent grouping candidates:" << '\n';
        for (auto& p: cand) {
            debug(0) << "[" << p.first << "," << p.second << "]" << '\n';
        }

        vector<pair<FusionChoice, EvalConfig>> best;
        best = choose_candidate_fuse(cand, level);

        if (!(best.size() > 0)) {
            continue;
        } else {
            fixpoint = false;
        }

        // TODO: state assumptions behind the following code
        string prod = best[0].first.prod;

        Function prod_f = dep_analy.env.at(prod);
        size_t num_stages = prod_f.updates().size() + 1;

        FStage final_stage(prod_f, num_stages - 1);
        set<FStage> cand_group_children = children[final_stage];

        // Invalidate entries of the fusion cache
        set<FusionChoice> invalid_keys;
        for (auto &c: cand_group_children) {
            for (auto &entry: fusion_cache) {
                if (entry.first.prod == c.func.name() || entry.first.cons == c)
                    invalid_keys.insert(entry.first);
            }
        }

        for (auto &key: invalid_keys) {
            fusion_cache.erase(key);
        }

        for (auto &fuse: best) {
            internal_assert(fuse.first.prod == prod);
            merge_groups(fuse.first, fuse.second, level);
        }

        for (size_t s = 0; s < num_stages; s++) {
            FStage cand_group(prod_f, s);
            groups.erase(cand_group);
            group_costs.erase(cand_group);

            // Update the children mapping
            children.erase(cand_group);
            for (auto &f: children) {
                set<FStage> &cons = f.second;
                if (cons.find(cand_group) != cons.end()) {
                    cons.erase(cand_group);
                    cons.insert(cand_group_children.begin(),
                                cand_group_children.end());
                }
            }
        }
    }
}

DimBounds Partitioner::get_bounds(const FStage& s) {

    Definition def = get_stage_definition(s.func, s.stage_num);
    DimBounds bounds;

    const vector<string>& args = s.func.args();
    for (size_t d = 0; d < args.size(); d++) {
        bounds[args[d]] = pipeline_bounds.at(s.func.name())[d];
    }

    for (const ReductionVariable& rvar: def.schedule().rvars()) {
        bounds[rvar.var] = Interval(simplify(rvar.min),
                                    simplify(rvar.min + rvar.extent - 1));
    }
    return bounds;
}

DimBounds
Partitioner::get_bounds_from_tile_sizes(const FStage &s,
                                        const map<string, int> &tile_sizes) {

    Definition def = get_stage_definition(s.func, s.stage_num);
    map<string, Interval> bounds;

    const map<string, Interval> &def_bounds = get_bounds(s);
    const vector<Dim> &dims = def.schedule().dims();

    for (int d = 0; d < (int) dims.size() - 1; d++) {
        string var = dims[d].var;
        const Interval &bound = def_bounds.at(var);
        if (tile_sizes.find(var) != tile_sizes.end()) {
            int size = tile_sizes.at(var);
            // Check if the bounds allow for tiling with the given tile size
            // i.e., ensure atleast 2 tiles
            int extent = get_extent(bound);
            if (extent >= 2 * size) {
                // TODO: Maybe shift this to the center of the pipeline bound
                bounds[var] = Interval(0, size - 1);
            }
            else {
                // If the dimension is too small do not tile it and set the
                // extent of the bounds to that of the dimension estimate
                bounds[var] = bound;
            }
        }
        else {
            bounds[var] = bound;
        }
    }

    return bounds;
}

Partitioner::GroupAnalysis Partitioner::analyze_group(const Group &g) {
    // Estimating the number of accesses to slow memory

    // 1) Assume all loads are a miss if the working set does not fit in cache.
    // This ignores any locality that results from the iteration order. This is
    // pretty aggresive in estimating the benefit of fusion.
    //
    // 2) Assume that the intermediates are loaded only once even if they do not
    // fit in cache. It is a pretty good model for pipelines which are streaming
    // in nature. This gives a conservative estimate of fusion benefit and does
    // not accurately capture scenarios where there is significant reuse.
    //
    // The actual number of accesses will inbetween 2) and 1) for now going with
    // model 1).
    //
    // TODO: Model needs to be refined further to account for spatial locality and
    // iteration order.

    // Get the definition corresponding to the group output
    Definition def = get_stage_definition(g.output.func, g.output.stage_num);

    set<string> group_inputs;
    set<string> group_mem;

    for (auto& stg: g.members) {
        group_mem.insert(stg.func.name());

        FindAllCalls find;
        Definition stg_def = get_stage_definition(stg.func, stg.stage_num);

        stg_def.accept(&find);
        for (auto& c: find.calls) {
            bool is_member = false;
            for (auto& m: g.members) {
                if (m.func.name() == c) {
                    is_member = true;
                    break;
                }
            }
            if (!is_member) {
                group_inputs.insert(c);
            }
        }
    }

    // Count the number of tiles
    uint64_t estimate_tiles = 1;
    uint64_t num_ele_per_tile = 1;

    const vector<Dim>& dims = def.schedule().dims();

    DimBounds stg_bounds = get_bounds(g.output);

    for (int d = 0; d < (int)dims.size() - 1; d++) {
        string var = dims[d].var;
        if (g.tile_sizes.find(var) != g.tile_sizes.end()) {
            int size = g.tile_sizes.at(var);
            int extent = get_extent(stg_bounds.at(var));
            estimate_tiles *= std::ceil((float)extent/size);
            num_ele_per_tile *= size;
        }
    }

    // Get the regions of the pipeline required to compute a tile of the group
    DimBounds tile_bounds = get_bounds_from_tile_sizes(g.output, g.tile_sizes);

    map<string, Box> conc_reg =
            dep_analy.regions_required(g.output.func, g.output.stage_num,
                                       tile_bounds, group_mem);

    map<string, Box> group_reg, prod_reg, input_reg;

    // Separating into regions that belong to the group and are input to the group
    for (auto &reg: conc_reg) {
        if (group_mem.find(reg.first) != group_mem.end() &&
            reg.first != g.output.func.name()) {
            group_reg[reg.first] = reg.second;
        } else if (group_inputs.find(reg.first) != group_inputs.end()) {
            if (dep_analy.env.find(reg.first) != dep_analy.env.end())
                prod_reg[reg.first] = reg.second;
            else
                input_reg[reg.first] = reg.second;
        }
    }

    debug(0) << g;
    debug(0) << "\nProd reg:" << '\n';
    disp_regions(prod_reg);
    debug(0) << "Input reg:" << '\n';
    disp_regions(input_reg);
    debug(0) << "Group reg:" << '\n';
    disp_regions(group_reg);

    // Compute the cost of the region and the size of the intermediates
    pair<int64_t, int64_t> tile_cost =
            cost_model.region_cost(group_reg, g.inlined);

    // TODO: This is inaccurate this can both over and under estimate the input
    // foot print
    int64_t tile_input_size = cost_model.region_size(prod_reg) +
                              cost_model.input_region_size(input_reg);

    Box out_tile_extent;
    const vector<string> &args = g.output.func.args();
    for (size_t d = 0; d < args.size(); d++ ) {
        if (tile_bounds.find(args[d]) != tile_bounds.end()) {
            out_tile_extent.push_back(tile_bounds[args[d]]);
        } else {
            out_tile_extent.push_back(Interval());
        }
    }

    int64_t tile_output_size = 0;

    // Compute the tile output size if the stage is not an update
    // otherwise it is already accounted for in the group regions
    if (g.output.stage_num == 0) {
        tile_output_size = cost_model.region_size(g.output.func.name(),
                                                  out_tile_extent);
    } else {
        tile_output_size =
                cost_model.region_size(g.output.func.name(),
                                       conc_reg.at(g.output.func.name()));
        // For an update definition the region of output touched is the
        // input from the previous stage
        tile_input_size += tile_output_size;
    }

    // Adding tile input and output size to tile_intermediate_size over
    // estimates the working set size
    int64_t group_inter_size = cost_model.region_size(group_reg, g.inlined);
    int64_t tile_inter_size = group_inter_size + tile_input_size +
                              tile_output_size;

    pair<int64_t, int64_t> out_cost =
            cost_model.stage_region_cost(g.output.func.name(),
                                         g.output.stage_num,
                                         tile_bounds, g.inlined);

    tile_cost.first += out_cost.first;
    debug(0) << "out_cost.second:" << out_cost.second << '\n';
    tile_cost.second += out_cost.second;

    GroupAnalysis g_analy;
    g_analy.arith_cost = -1;
    g_analy.mem_cost = -1;
    g_analy.parallelism = -1;

    // The group could not be analyzed
    if (tile_cost.first < 0 || tile_cost.second < 0 ||
        tile_input_size < 0 || tile_inter_size < 0 ||
        out_cost.first < 0 || out_cost.second < 0 ||
        tile_output_size < 0) {
        return g_analy;
    }

    int64_t per_tile_mem_cost = 0;
    int64_t per_tile_arith_cost = tile_cost.first;

    /*
    if (tile_inter_size > arch_params.l1_size) {
        // Conservative estimate of accesses to memory
        //per_tile_mem_cost = tile_inter_size;
        // Aggressive estimate of accesses to memory
        per_tile_mem_cost = tile_cost.second;
    } else {
        // The tile_input_size captures the region of the input
        // required to compute the tile. However, all of it many not be
        // accessed during the computation of the tile when the access
        // is sparse. A better estimate is given by the smaller of
        // the number of memory accesses and the region size
        per_tile_mem_cost = std::min(tile_input_size + tile_output_size,
                                     tile_cost.second);
    }*/

    // Towards cache oblivious cost

    // TODO: Use smooth step curve from Jon

    /*
    // Log dropoff
    float max_load_cost = std::log2((float)arch_params.last_level_size/
                                    arch_params.register_file_size);

    int64_t output_footprint = tile_output_size * estimate_tiles;
    int64_t input_footprint = std::min(tile_input_size, tile_cost.second) * estimate_tiles;

    float output_rel_size = (float)output_footprint/arch_params.register_file_size;
    float input_rel_size = (float)input_footprint/arch_params.register_file_size;;
    float tile_rel_size = (float)tile_inter_size/arch_params.register_file_size;


    float input_load_cost =
            std::max(std::min((float)std::log2(input_rel_size), max_load_cost), 0.0f);
    float output_load_cost =
            std::max(std::min((float)std::log2(output_rel_size), max_load_cost), 0.0f);
    float inter_load_cost =
            std::max(std::min((float)std::log2(tile_rel_size), max_load_cost), 0.0f);*/

    // Linear dropoff
    float load_slope = (float)arch_params.balance/arch_params.last_level_size;

    int64_t output_footprint = tile_output_size * estimate_tiles;

    // TODO: input_footprint is inaccurate it can both over and under estimate
    // the input foot print the tile_cost.second it the cost of the whole group
    // not just the number of bytes loaded from inputs.
    int64_t input_footprint = std::min(tile_input_size, tile_cost.second) * estimate_tiles;

    float input_load_cost =
            std::min(1 + input_footprint * load_slope, (float)arch_params.balance);
    float output_load_cost =
            std::min(1 + output_footprint * load_slope, (float)arch_params.balance);
    float inter_load_cost =
            std::min(1 + tile_inter_size * load_slope, (float)arch_params.balance);

    per_tile_mem_cost += std::min(tile_input_size, tile_cost.second) * input_load_cost;
    per_tile_mem_cost += tile_output_size * output_load_cost;
    per_tile_mem_cost += tile_cost.second * inter_load_cost;

    g_analy.mem_cost = per_tile_mem_cost * estimate_tiles;
    g_analy.arith_cost = per_tile_arith_cost * estimate_tiles;
    g_analy.parallelism = estimate_tiles;

    debug(0) << "input_load_cost:" << input_load_cost << '\n';
    debug(0) << "output_load_cost:" << output_load_cost << '\n';
    debug(0) << "inter_load_cost:" << inter_load_cost << '\n';
    debug(0) << "intermediate size:" << tile_inter_size << '\n';
    debug(0) << "per_tile_arith_cost:" << per_tile_arith_cost << '\n';
    debug(0) << "per_tile_mem_cost:" << per_tile_mem_cost << '\n';
    debug(0) << "tile_cost.second:" << tile_cost.second << '\n';
    debug(0) << "tile_input_size:" << tile_input_size << '\n';
    debug(0) << "tile_output_size:" << tile_output_size << '\n';
    debug(0) << g_analy << '\n';

    //internal_assert(per_tile_mem_cost > 0);

    return g_analy;
}

Partitioner::Group Partitioner::fuse_groups(Group& prod_group,
                                            Group& cons_group) {

    vector<FStage> fused_members;
    for (auto& s: prod_group.members)
        fused_members.push_back(s);
    for (auto& s: cons_group.members)
        fused_members.push_back(s);

    Group fused_group(cons_group.output, fused_members);

    for (auto& f: prod_group.inlined)
        fused_group.inlined.insert(f);
    for (auto& f: cons_group.inlined)
        cons_group.inlined.insert(f);

    return fused_group;
}

Partitioner::EvalConfig
Partitioner::evaluate_choice(const FusionChoice& choice,
                             Partitioner::Level level) {

    // Create a group that reflects the fusion choice and evaluate the cost
    // of the group.
    Function prod_f = dep_analy.env.at(choice.prod);
    int num_prod_stages = prod_f.updates().size() + 1;
    vector<Group> prod_groups;
    for (int s = 0 ; s < num_prod_stages; s++) {
        FStage prod_s(prod_f, s);
        prod_groups.push_back(groups.at(prod_s));
    }

    Group cons = groups.at(choice.cons);
    Group fused = cons;
    for (auto &prod_g: prod_groups) {
        fused = fuse_groups(prod_g, fused);
    }

    GroupAnalysis fused_analy;
    map<string, int> best_tile_config;

    if (level == Partitioner::INLINE) {
        // Set the tile sizes to one along all dimensions of the consumer group
        map<string, int> tile_sizes;

        const Function &cons_f = cons.output.func;
        Definition def = get_stage_definition(cons_f,
                                              cons.output.stage_num);

        const vector<Dim> &dims = def.schedule().dims();
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            tile_sizes[dims[d].var] = 1;
        }

        fused.tile_sizes = tile_sizes;

        for (auto &prod_g: prod_groups) {
            for (const FStage& s: prod_g.members)
                fused.inlined.insert(s.func.name());
        }

        for (const string& f: cons.inlined)
            fused.inlined.insert(f);

        fused_analy = analyze_group(fused);
        best_tile_config = tile_sizes;

    } else {
        pair<map<string, int>, GroupAnalysis> config =
                                    find_best_tile_config(fused);
        best_tile_config = config.first;
        fused_analy = config.second;
    }

    int64_t benefit = estimate_benefit(prod_groups, cons, fused_analy,
                                       false, true);

    /*
    TODO: come up with a better assert. Currently this will hit when
    the whole function fits in the tile and tiling is equivalent to
    no tiling
    internal_assert(benefit <= 0 ||
                     (benefit > 0 && best_tile_config.size() > 0));
    */

    return EvalConfig(best_tile_config, benefit);
}

int64_t Partitioner::estimate_benefit(const GroupAnalysis &nofuse,
                                      const GroupAnalysis &fuse,
                                      bool no_redundant_work,
                                      bool ensure_parallelism) {

    debug(0) << "No fuse analysis:" << nofuse << '\n';
    debug(0) << "fuse analysis:" << fuse << '\n';
    if (ensure_parallelism &&
        fuse.parallelism < arch_params.parallelism) {
        return -1;
    }

    int64_t arith_benefit = 0;
    if (nofuse.arith_cost >= 0 && fuse.arith_cost >= 0) {
        arith_benefit = nofuse.arith_cost - fuse.arith_cost;
    } else {
        return -1;
    }

    if (no_redundant_work && arith_benefit < 0)
        return arith_benefit;

    int64_t mem_benefit = 0;
    if (nofuse.mem_cost >= 0 && fuse.mem_cost >= 0) {
        mem_benefit = nofuse.mem_cost - fuse.mem_cost;
    } else {
        return -1;
    }

    return mem_benefit + arith_benefit;
}

int64_t Partitioner::estimate_benefit(const vector<Group> &prod_groups,
                                      const Group &cons_group,
                                      const GroupAnalysis &fused_analy,
                                      bool no_redundant_work,
                                      bool ensure_parallelism) {

    internal_assert(group_costs.find(cons_group.output) != group_costs.end());
    GroupAnalysis cons_analy = group_costs.at(cons_group.output);

    int64_t prod_arith_cost = 0;
    int64_t prod_mem_cost = 0;
    int64_t prod_par = std::numeric_limits<int64_t>::max();

    //debug(0) << "Prod groups:" << '\n';
    for (auto &prod_g: prod_groups) {
        internal_assert(group_costs.find(prod_g.output) != group_costs.end());
        GroupAnalysis analyg = group_costs.at(prod_g.output);
        if (analyg.arith_cost >= 0) {
            prod_arith_cost += analyg.arith_cost;
            prod_mem_cost += analyg.mem_cost;
            prod_par = std::min(prod_par, analyg.parallelism);
        } else {
            prod_arith_cost = -1;
            prod_mem_cost = -1;
            prod_par = -1;
            break;
        }
        //debug(0) << prod_g;
    }

    //debug(0) << "Cons group:" << cons_group << '\n';

    GroupAnalysis no_fuse_analy;
    no_fuse_analy.arith_cost = prod_arith_cost + cons_analy.arith_cost;
    no_fuse_analy.mem_cost = prod_mem_cost + cons_analy.mem_cost;
    no_fuse_analy.parallelism = std::min(prod_par, cons_analy.parallelism);

    //debug(0) << "No fuse analysis:" << no_fuse_analy << '\n';
    //debug(0) << "fuse analysis:" << fused_analy << '\n';

    return estimate_benefit(no_fuse_analy, fused_analy, no_redundant_work,
                            ensure_parallelism);
}

map<string, int> Partitioner::bounds_to_estimates(const DimBounds &bounds) {
    map<string, int> estimates;
    for (auto &bound: bounds) {
        int estimate = get_extent(bound.second);
        estimates[bound.first] = estimate;
    }
    return estimates;
}

map<FStage, map<FStage, DimBounds>> Partitioner::get_group_member_bounds() {

    map<FStage, map<FStage, DimBounds>> group_bounds;
    for (const pair<const FStage, Group> &gpair: groups) {
        Group g = gpair.second;
        map<FStage, DimBounds> mem_bounds;

        DimBounds bounds = get_bounds_from_tile_sizes(g.output, g.tile_sizes);

        set<string> prods;
        for (const FStage &s: g.members) {
            prods.insert(s.func.name());
        }

        map<string, Box> conc_reg =
                dep_analy.regions_required(g.output.func, g.output.stage_num,
                                           bounds, prods);

        for (const FStage &s: g.members) {
            if (conc_reg.find(s.func.name()) != conc_reg.end()) {
                map<string, int> tile_sizes;
                const vector<string> &args = s.func.args();
                for (size_t arg = 0; arg < args.size(); arg++) {
                    tile_sizes[args[arg]] = get_extent(conc_reg[s.func.name()][arg]);
                }
                mem_bounds[s] = get_bounds_from_tile_sizes(s, tile_sizes);
            }
        }

        group_bounds[gpair.first] = mem_bounds;
    }

    return group_bounds;
}

string get_base_name(string name) {
    size_t dot_pos = name.rfind('.');
    if (dot_pos != string::npos) {
        return name.substr(dot_pos + 1);
    }
    return name;
}

pair<VarOrRVar, VarOrRVar>
split_dim(Stage f_handle, VarOrRVar v, int factor, string in_suffix,
          string out_suffix, map<string, int> &estimates, string &sched) {
    // Create new variables for the split dimensions
    string arg_name = v.name();
    string inner_name = arg_name + in_suffix;
    string outer_name = arg_name + out_suffix;
    VarOrRVar inner(inner_name), outer(outer_name);

    sched += "Var " + inner_name + "(\"" + outer_name + "\")" + ";\n";
    sched += "Var " + outer_name + "(\"" + outer_name + "\")" + ";\n";

    f_handle.split(v, outer, inner, factor);

    sched += f_handle.name() + ".split(" + arg_name + ',' +
             outer_name + ',' + inner_name +
            ',' + std::to_string(factor) + ";\n";

    internal_assert(estimates.find(arg_name) != estimates.end());

    estimates[inner_name] = factor;
    estimates[outer_name] =
            std::ceil((float)estimates.at(arg_name)/factor);
    estimates.erase(arg_name);

    return make_pair(inner, outer);
}

void vectorize_stage(Stage f_handle, Definition def, Function func,
                     const Target &t, set<string> &rvars,
                     map<string, int> &estimates, string &sched) {
    const vector<Dim> &dims = f_handle.get_schedule().dims();
    int vec_dim_index = -1;

    int vec_len = 0;
    for (auto &type: func.output_types()) {
        vec_len = std::max(vec_len, t.natural_vector_size(type));
    }

    for (int d = 0; d < (int) dims.size() - 1; d++) {
        string dim_name = get_base_name(dims[d].var);
        bool can_vectorize = true;
        if (rvars.find(dim_name) != rvars.end()) {
            can_vectorize = can_parallelize_rvar(dim_name, func.name(), def);
        }
        if (estimates.find(dim_name) != estimates.end()) {
            if (can_vectorize && estimates[dim_name] >= vec_len) {
                vec_dim_index = d;
                break;
            }
        }
    }

    if (vec_dim_index >= 0) {
        string vec_dim_name = get_base_name(dims[vec_dim_index].var);
        Var vec_var(vec_dim_name);
        // Set the vector length as the maximum of the values produced by a
        // function

        //if (estimates[vec_dim_name] > 2*vec_len) {
            bool is_rvar = (rvars.find(vec_dim_name) != rvars.end());

            pair<VarOrRVar, VarOrRVar> split_vars =
                    split_dim(f_handle, vec_var, vec_len, "_vi", "_vo",
                              estimates, sched);

            f_handle.vectorize(split_vars.first);
            sched += f_handle.name() + ".vectorize(" +
                     split_vars.first.name() + ");\n";

            if (is_rvar) {
                rvars.erase(vec_dim_name);
                rvars.insert(split_vars.first.name());
                rvars.insert(split_vars.second.name());
            }
        /*
        } else {
            f_handle.vectorize(vec_var);
            sched += f_handle.name() + ".vectorize(" + vec_var.name() + ");\n";
        }
        debug(0) << "Got vectorized:" << f_handle.name() << "," <<
                 vec_var.name() << "," << estimates[vec_dim_name] << '\n';
        */
    }
}

string Partitioner::generate_group_cpu_schedule(
                    const Group &g, const Target &t,
                    const map<FStage, DimBounds> &group_bounds) {

    string sched = "";
    string out_f_name = g.output.func.name();
    Function g_out = g.output.func;

    debug(0) << "\nScheduling group:" << g;

    // Get the definition corresponding to the stage
    Definition def = get_stage_definition(g_out,
                                          g.output.stage_num);

    // Get the estimates for stage bounds
    DimBounds stg_bounds = get_bounds(g.output);
    map<string, int> stg_estimates = bounds_to_estimates(stg_bounds);

    Stage f_handle = Stage(Func(g_out));

    // Get a function handle for scheduling the stage
    if (g.output.stage_num > 0) {
        int stage_num = g.output.stage_num;
        f_handle = Func(g_out).update(stage_num - 1);
    } else {
        Func(g_out).compute_root();
        sched += f_handle.name() + ".compute_root()" + ";\n";
    }

    string var_prefix = g_out.name() + "_" +
                        std::to_string(g.output.stage_num);

    // Realize tiling and update the dimension estimates
    vector<VarOrRVar> outer_dims;
    vector<VarOrRVar> inner_dims;

    vector<Dim> &dims = def.schedule().dims();

    // Keep track of the rvars
    set<string> rvars;
    for (int d = 0; d < (int) dims.size() - 1; d++) {
        bool is_pure_var = false;
        for (auto &arg: g_out.args()) {
            if (arg == get_base_name(dims[d].var)) {
                is_pure_var = true;
                break;
            }
        }
        if (!is_pure_var) {
            rvars.insert(get_base_name(dims[d].var));
        }
    }

    vector<string> dim_vars;
    for (int d = 0; d < (int) dims.size() - 1; d++) {
        dim_vars.push_back(get_base_name(dims[d].var));
    }

    for (auto &var: dim_vars) {
        bool is_rvar = (rvars.find(var) != rvars.end());
        VarOrRVar v(var, is_rvar);

        if (g.tile_sizes.find(var) != g.tile_sizes.end() &&
            stg_estimates.at(var) > g.tile_sizes.at(var)) {
            int tile_size = g.tile_sizes.at(var);
            if (tile_size > 1) {
                pair<VarOrRVar, VarOrRVar> tile_vars =
                        split_dim(f_handle, v, tile_size, "_i", "_o",
                                  stg_estimates, sched);

                inner_dims.push_back(tile_vars.first);
                outer_dims.push_back(tile_vars.second);

                if (is_rvar) {
                    rvars.erase(var);
                    rvars.insert(tile_vars.first.name());
                    rvars.insert(tile_vars.second.name());
                }
            } else {
                outer_dims.push_back(v);
            }
        } else {
            inner_dims.push_back(v);
        }
    }

    // Reorder the tile dimensions
    if (outer_dims.size() > 0) {

        vector<VarOrRVar> ordering;
        for (auto& v: inner_dims)
            ordering.push_back(v);
        for (auto& v: outer_dims)
            ordering.push_back(v);

        string var_order = ordering[0].name();
        for (size_t o = 1; o < ordering.size(); o++) {
            var_order += ',' + ordering[o].name();
        }

        f_handle.reorder(ordering);
        sched += f_handle.name() + ".reorder(" + var_order + ");\n";
    }

    vectorize_stage(f_handle, def, g_out, t, rvars, stg_estimates, sched);

    // Parallelize definition
    uint32_t def_par = 1;
    // TODO: Investigate if it is better to pull one large dimension and
    // parallelize over it or generate nested parallelism
    //
    // Go from the outer to the inner most loop till sufficient parallelism
    // is achieved
    int dim_start = dims.size() - 2;
    for (int d = dim_start; d >= 0; d--) {
        string var = get_base_name(dims[d].var);
        bool is_rvar = (rvars.find(var) != rvars.end());
        VarOrRVar v(var, is_rvar);

        if (is_rvar && !can_parallelize_rvar(var, g_out.name(), def)) {
            break;
        }

        if (def_par >= arch_params.parallelism) {
            // Enough parallelism to saturate target machine
            break;
        }

        if (stg_estimates.find(var) != stg_estimates.end()) {
            f_handle.parallel(v);
            sched += f_handle.name() + ".parallel(" + var + ");\n";
            def_par *= stg_estimates[var];
        } else {
            break;
        }
    }

    if (def_par < arch_params.parallelism) {
        debug(0) << "Warning: insuffcient parallelism for " <<
                 f_handle.name() << '\n';
    }

    // The level at which group members will be computed
    int tile_inner_index = dims.size() - outer_dims.size() - 1;
    VarOrRVar tile_inner_var("", false);
    if (outer_dims.size() > 0) {
        string var_name = get_base_name(dims[tile_inner_index].var);
        bool is_rvar = (rvars.find(var_name) != rvars.end());
        tile_inner_var = VarOrRVar(var_name, is_rvar);
    }

    for (const FStage &mem: g.members) {
        // Skip member stages that have been inlined
        if (g.inlined.find(mem.func.name()) != g.inlined.end() ||
            mem.func.name() == g_out.name())
            continue;

        // Get the definition corresponding to the stage
        Definition mem_def = get_stage_definition(mem.func, mem.stage_num);

        // Get the estimates for the dimensions of the member stage
        map<string, int> mem_estimates =
                bounds_to_estimates(group_bounds.at(mem));

        set<string> mem_rvars;
        const vector<Dim> &mem_dims = mem_def.schedule().dims();
        for (int d = 0; d < (int) mem_dims.size() - 1; d++) {
            bool is_pure_var = false;
            for (auto &arg: mem.func.args()) {
                if (arg == get_base_name(mem_dims[d].var)) {
                    is_pure_var = true;
                    break;
                }
            }
            if (!is_pure_var) {
                mem_rvars.insert(get_base_name(mem_dims[d].var));
            }
        }

        // Get a function handle for scheduling the stage
        Stage mem_handle = Stage(Func(mem.func));
        if (mem.stage_num > 0) {
            mem_handle = Func(mem.func).update(mem.stage_num - 1);
        } else {
            if (outer_dims.size() > 0) {
                if (tile_inner_var.is_rvar) {
                    Func(mem.func).compute_at(Func(g_out), tile_inner_var.rvar);
                } else {
                    Func(mem.func).compute_at(Func(g_out), tile_inner_var.var);
                }
                sched += mem_handle.name() + ".compute_at(" + g_out.name() +
                        ',' + tile_inner_var.name() + ");\n";
            } else {
                debug(0) << "Warning: Degenerate tiling no dimensions are tiled" << '\n';
                debug(0) << "Computing " <<  mem.func.name() << " at root" << '\n';
                Func(mem.func).compute_root();
                sched += mem_handle.name() + ".compute_root()";
            }
        }

        vectorize_stage(mem_handle, mem_def, mem.func, t, mem_rvars,
                        mem_estimates, sched);
    }

    return sched;
}

string Partitioner::generate_cpu_schedule(const Target& t) {
    string sched = "";

    // Grab the bounds early as they rely on the dimensions of the group outputs
    // which will be altered by modifying schedules
    map<FStage, map<FStage, DimBounds>> group_bounds =
                                        get_group_member_bounds();

    for (const pair<FStage, Group>& g: groups) {
        for (const string& inline_func: g.second.inlined) {
            Function f = dep_analy.env.at(inline_func);
            Func f_handle(f);
            // TODO: inling functions with update definitions has different
            // behavior than pure functions. They may need to be computed above
            // the inner most vector loop to avoid complications with varying
            // extents across different vector lanes.

            // The default is compute inline but setting it explicitly
            f_handle.compute_inline();
            sched += f_handle.name() + ".compute_inline()" + ";\n";
        }
    }

    for (auto& g: groups)
        sched += generate_group_cpu_schedule(g.second, t, group_bounds[g.first]);
    return sched;
}

void generate_schedules(const vector<Function>& outputs, const Target& target) {

    // Compute an environment map which is used throughout the auto scheduling
    // process
    map<string, Function> env;
    for (Function f : outputs) {
        map<string, Function> more_funcs = find_transitive_calls(f);
        env.insert(more_funcs.begin(), more_funcs.end());
    }

    vector<string> order = realization_order(outputs, env);

    FuncValueBounds func_val_bounds = compute_function_value_bounds(order, env);

    bool estimates_avail = check_estimates_on_outputs(outputs);

    if (!estimates_avail) {
        user_warning << "Please provide estimates for each dimension" <<
                        "of the pipeline output functions.\n";
        return;
    }

    map<string, vector<string> > update_args;
    set<string> reductions;
    DependenceAnalysis dep_analy(env, func_val_bounds);

    // Compute bounds of all the functions in the pipeline given estimates
    // on outputs. Also report functions where the bounds could not be inferred.
    map<string, Box> pipeline_bounds = get_pipeline_bounds(dep_analy, outputs);

    // Set machine parameters
    // TODO: Expose machine parameters to the user
    MachineParams arch_params;
    arch_params.parallelism = 16;
    arch_params.vec_len = 8;
    arch_params.register_file_size = 1024; // 1KB
    arch_params.last_level_size = 8 * 1024 * 1024; // 8MB
    arch_params.balance = 40;

    // Initialize the cost model
    // Compute the expression costs for each function in the pipeline
    CostModel cost_model(env);
    cost_model.disp_costs();

    Partitioner part(pipeline_bounds, arch_params, dep_analy,
                     cost_model, outputs, false);

    // Compute and display reuse
    /*
    for (auto& f: env) {
        FindAllCalls find;
        f.second.accept(&find);
        int num_stages = f.second.updates().size() + 1;
        for (int s = 0; s < num_stages; s++) {
            FStage curr_s(f.second, s);
            map<string, int64_t> reuse =
                    part.evaluate_reuse(curr_s, find.calls);
            debug(0) << curr_s << '\n';
            for (auto& dir: reuse) {
                debug(0) << dir.first << " " << dir.second << ',';
            }

            debug(0) << '\n';
        }
    }*/

    // Show the current pipeline graph
    // TODO: Output the graph in dot format
    part.disp_pipeline_graph();
    part.disp_pipeline_bounds();

    part.initialize_groups_inline();
    part.disp_pipeline_costs();

    part.group(Partitioner::INLINE);
    part.disp_grouping();

    part.initialize_groups_fast_mem();
    part.group(Partitioner::FAST_MEM);

    part.disp_pipeline_costs();
    string sched = part.generate_cpu_schedule(target);
    return;


    part.disp_grouping();

    // TODO: Auto scheduler modes
    // O3 Trades-offs redundant work for enhancing locality and parallelism

    // TODO: Better handling of boundary conditions
    // TODO: GPU scheduling

    // Set the schedule defaults for each function in the environment
    //set_schedule_defaults(env);
    debug(0) << sched << '\n';
}

}
}
