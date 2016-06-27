#include "RegionCosts.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::pair;
using std::set;
using std::vector;
using std::make_pair;
using std::deque;

class ExprCost : public IRVisitor {
 public:
  int64_t ops;
  int64_t byte_loads;
  map<string, int64_t> detailed_byte_loads;

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
          if (detailed_byte_loads.find(call->name) ==
              detailed_byte_loads.end()) {
              detailed_byte_loads[call->name] = call->type.bytes();
          } else {
              detailed_byte_loads[call->name] += call->type.bytes();
          }
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

  // Should not hit any of these IR nodes when used at the level at
  // which the auto scheduler operates
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

// Utility functions
int64_t get_func_value_size(const Function &f) {
    int64_t size = 0;
    const vector<Type> &types = f.output_types();
    for (size_t i = 0; i < types.size(); i++)
        size += types[i].bytes();
    internal_assert(types.size() != 0);
    return size;
}

int get_extent(const Interval &i) {
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

int64_t box_area(const Box &b) {
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

void disp_regions(map<string, Box> &regions) {
    for (auto &reg: regions) {
        debug(0) << reg.first;
        debug(0) << reg.second;
        debug(0) << "\n";
    }
}

Definition get_stage_definition(const Function &f, int stage_num) {
    if (stage_num == 0) {
        return f.definition();
    }
    internal_assert((int)f.updates().size() >= stage_num);
    return f.updates()[stage_num - 1];
}

RegionCosts::RegionCosts(const map<string, Function> &_env) : env(_env) {
    for (auto &kv : env) {
        func_cost[kv.first] = get_func_cost(kv.second);

        FindImageInputs find;
        kv.second.accept(&find);
        for (auto& in: find.input_type) {
            inputs[in.first] = in.second;
        }
    }
}

Expr RegionCosts::perform_inline(Expr e, const set<string> &inlines) {

    if (inlines.empty())
        return e;

    bool funcs_to_inline = false;
    Expr inlined_expr = e;

    do {
        funcs_to_inline = false;
        FindAllCalls find;
        inlined_expr.accept(&find);
        set<string> &calls = find.calls;
        for (auto &call: calls) {
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

pair<int64_t, int64_t>
RegionCosts::stage_region_cost(string func, int stage,
                               DimBounds &bounds, const set<string> &inlines) {
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

pair<int64_t, int64_t>
RegionCosts::stage_region_cost(string func, int stage, Box &region,
                                 const set<string> &inlines) {

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


pair<int64_t, int64_t>
RegionCosts::region_cost(string func, Box &region, const set<string> &inlines) {

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

pair<int64_t, int64_t>
RegionCosts::region_cost(map<string, Box> &regions, const set<string> &inlines) {

    pair<int64_t, int64_t> total_cost(0, 0);
    for (auto &f: regions) {
        // The cost for pure inlined functions will be accounted in the
        // consumer of the inlined function
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

void combine_load_costs(map<string, int64_t> &result,
                        const map<string, int64_t> &partial) {
    for (auto &kv: partial) {
        if (result.find(kv.first) == result.end()) {
            result[kv.first] = kv.second;
        } else {
            if (kv.second >= 0) {
                result[kv.first] += kv.second;
            } else {
                result[kv.first] = -1;
            }
        }
    }
}

map<string, int64_t>
RegionCosts::stage_detailed_load_costs(string func, int stage,
                                       const set<string> &inlines) {
    map<string, int64_t> load_costs;
    Function curr_f = env.at(func);
    Definition def = get_stage_definition(curr_f, stage);

    for (auto &e: def.values()) {
        Expr inlined_expr = perform_inline(e, inlines);
        ExprCost cost_visitor;
        inlined_expr.accept(&cost_visitor);
        const map<string, int64_t> &expr_load_costs =
                        cost_visitor.detailed_byte_loads;

        combine_load_costs(load_costs, expr_load_costs);

        if (load_costs.find(func) ==
            load_costs.end()) {
            load_costs[func] = e.type().bytes();
        } else {
            load_costs[func] += e.type().bytes();
        }
    }

    return load_costs;
}

map<string, int64_t>
RegionCosts::stage_detailed_load_costs(string func, int stage,
                                       DimBounds &bounds,
                                       const set<string> &inlines) {
    Function curr_f = env.at(func);
    Definition def = get_stage_definition(curr_f, stage);

    Box stage_region;

    const vector<Dim> &dims = def.schedule().dims();
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        stage_region.push_back(bounds.at(dims[d].var));
    }

    map<string, int64_t> load_costs =
            stage_detailed_load_costs(func, stage, inlines);

    int64_t area = box_area(stage_region);
    for (auto &kv: load_costs) {
        if (area >= 0) {
            load_costs[kv.first] *= area;
        } else {
            load_costs[kv.first] = -1;
        }
    }

    return load_costs;
}

map<string, int64_t>
RegionCosts::detailed_load_costs(string func, const Box &region,
                                 const set<string> &inlines) {

    Function curr_f = env.at(func);
    map<string, int64_t> load_costs;

    int num_stages = curr_f.updates().size() + 1;
    for (int s = 0; s < num_stages; s++) {

        map<string, int64_t> stage_load_costs =
                        stage_detailed_load_costs(func, s, inlines);

        combine_load_costs(load_costs, stage_load_costs);
    }

    int64_t area = box_area(region);
    for (auto &kv: load_costs) {
        if (area >= 0) {
            load_costs[kv.first] *= area;
        } else {
            load_costs[kv.first] = -1;
        }
    }

    return load_costs;
}

map<string, int64_t>
RegionCosts::detailed_load_costs(const map<string, Box> &regions,
                                 const set<string> &inlines) {
    map<string, int64_t> load_costs;
    for (auto &r: regions) {
        // The cost for pure inlined functions will be accounted in the
        // consumer of the inlined function
        if (inlines.find(r.first) != inlines.end() &&
            env.at(r.first).is_pure())
            continue;

        map<string, int64_t> partial_load_costs =
                detailed_load_costs(r.first, r.second, inlines);

        combine_load_costs(load_costs, partial_load_costs);
    }

    return load_costs;
}

vector<pair<int64_t, int64_t>>
RegionCosts::get_func_cost(const Function &f, const set<string> &inlines) {

    vector<pair<int64_t, int64_t>> func_costs;
    int64_t total_ops = 0;
    int64_t total_bytes = 0;
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

int64_t RegionCosts::region_size(string func, const Box &region) {
    const Function &f = env.at(func);
    int64_t area = box_area(region);
    if (area < 0) {
        // Area could not be determined
        return -1;
    }
    int64_t size_per_ele = get_func_value_size(f);
    return area * size_per_ele;
}

int64_t RegionCosts::region_footprint(const map<string, Box> &regions,
                                      const set<string> &inlined) {

    map<string, int> num_consumers;
    for (auto &f: regions)
        num_consumers[f.first] = 0;

    for (auto &f: regions) {
        map<string, Function> prods = find_direct_calls(env.at(f.first));
        for (auto &p: prods) {
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

    for (auto &f: regions) {
        // Inlined functions do not have allocations
        int64_t size = inlined.find(f.first) != inlined.end()? 0:
                region_size(f.first, f.second);
        if (size < 0)
            return -1;
        else
            func_sizes[f.first] = size;
    }

    for (auto &f: order) {
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

int64_t RegionCosts::input_region_size(string input, const Box &region) {
    int64_t area = box_area(region);
    if (area < 0) {
        // Area could not be determined
        return -1;
    }
    int64_t size_per_ele = inputs.at(input).bytes();
    return area * size_per_ele;
}

int64_t RegionCosts::input_region_size(const map<string, Box> &input_regions) {
    int64_t total_size = 0;
    for (auto &reg: input_regions) {
        int64_t size = input_region_size(reg.first, reg.second);
        if (size < 0) {
            return -1;
        } else {
            total_size += size;
        }
    }
    return total_size;
}

void RegionCosts::disp_func_costs() {
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

}
}
