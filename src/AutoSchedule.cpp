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
  /* TODO Check if this is necessary at some point
     else {
     Expr diff = simplify(i.max - i.min);
     if (diff.as<IntImm>())
     return diff.as<IntImm>()->value;
     } */
  return -1;
}

int64_t box_area(Box &b) {
  int64_t box_area = 1;
  for(size_t i = 0; i < b.size(); i++) {
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

void set_schedule_defaults(map<string, Function> &env) {
  // Changing the default to compute root.

  // TODO: This ignores existing schedules specified by
  // the user atm and needs to be addressed when we have
  // decided on a mechanism to inform the auto scheduler
  // not to mess with an user specified schedule.
  for (auto& kv : env) {
    // The shedule is marked touched when a user modifies
    // the schedule. The idea is to keep the user specified
    // schedule intact as much as possible.
    // TODO: However, user specified schedules can have
    // non-local effects and will not be captured by touched.
    // For example:
    // f.compute_at(g, y) now specifies constraints on the
    // schedule of f as well as the schedule of g i.e., the
    // variable y in g cannot be split or reordered since that
    // may change user intent.
    //
    // Open question is how to deal with the constraints induced
    // by user specified schedules.
    kv.second.schedule().store_level().func = "";
    kv.second.schedule().store_level().var = "__root";
    kv.second.schedule().compute_level().func = "";
    kv.second.schedule().compute_level().var = "__root";

    // Initializing the schedules for update definitions
    for (size_t u = 0; u < kv.second.updates().size(); u++) {
      kv.second.update_schedule(u).store_level().func = "";
      kv.second.update_schedule(u).store_level().var = "__root";
      kv.second.update_schedule(u).compute_level().func = "";
      kv.second.update_schedule(u).compute_level().var = "__root";
    }
  }
}

bool check_estimates_on_outputs(const vector<Function> &outputs) {
  bool estimates_avail = true;
  for (auto &out : outputs) {
    const vector<Bound> &estimates = out.schedule().estimates();
    if (estimates.size() != out.args().size()) {
      estimates_avail = false;
      break;
    }
    vector<string> vars = out.args();

    for (unsigned int i = 0; i < estimates.size(); i++) {
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

struct MachineParams {
  unsigned int parallelism;
  unsigned int vec_len;
  unsigned int fast_mem_size;
  unsigned int balance;
};

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

struct AbstractCost {

  /* Visitor for computing the arithmetic cost of a single value of a function*/
  class ExprCost : public IRVisitor {
   public:
    int ops;
    int loads;

    ExprCost() {
      ops = 0; loads = 0;
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

    // TODO: Figure out the right costs
    void visit(const Call * call) {
      if (call->call_type == Call::Halide) {
        loads+=1;
      } else if (call->call_type == Call::Extern) {
        ops+=1;
      } else if (call->call_type == Call::Image) {
        loads+=1;
      } else if (call->call_type == Call::Intrinsic) {
        ops+=1;
      }
      for (size_t i = 0; (i < call->args.size()); i++)
        call->args[i].accept(this);
    }

    void visit(const Let * let) {
      let->value.accept(this);
      let->body.accept(this);
    }
    // Should not hit any of these IR nodes at this
    // stage of compilation
    void visit(const Load *) { assert(0); }
    void visit(const Ramp *) { assert(0); }
    void visit(const Broadcast *) { assert(0); }
    void visit(const LetStmt *) { assert(0); }
    void visit(const AssertStmt *) {}
    void visit(const ProducerConsumer *) { assert(0); }
    void visit(const For *) { assert(0); }
    void visit(const Store *) { assert(0); }
    void visit(const Provide *) { assert(0); }
    void visit(const Allocate *) { assert(0); }
    void visit(const Free *) { assert(0); }
    void visit(const Realize *) { assert(0); }
    void visit(const Block *) { assert(0); }
    void visit(const IfThenElse *) { assert(0); }
    void visit(const Evaluate *) { assert(0); }
  };

  map<string, pair<int64_t, int64_t> > func_cost;

  pair<int, int> get_expr_cost(Expr e) {
    ExprCost cost_visitor;
    e.accept(&cost_visitor);
    return make_pair(cost_visitor.ops, cost_visitor.loads);
  }

  // TODO: Fix this for reductions
  int64_t region_cost(string func, Box &region) {

    int64_t area = box_area(region);
    if (area < 0) {
      // Area could not be determined
      return -1;
    }
    int64_t op_cost = func_cost[func].first;

    int64_t cost = area * (op_cost);
    assert(cost >= 0);
    return cost;
  }

  int64_t region_cost(map<string, Box> &regions) {

    int64_t total_cost = 0;
    for(auto &f: regions) {
      int64_t cost = region_cost(f.first, f.second);
      if (cost < 0) {
        return -1;
      }
      else
        total_cost += cost;
    }
    assert(total_cost >= 0);
    return total_cost;
  }

  AbstractCost(const map<string, Function> &env) {

    for (auto& kv : env) {

      func_cost[kv.first].first = 1;
      func_cost[kv.first].second = 0;

      // TODO: revist how boundary conditions are handled
      for (auto &e: kv.second.values()) {
        ExprCost cost_visitor;
        e.accept(&cost_visitor);
        func_cost[kv.first].first += cost_visitor.ops;
        func_cost[kv.first].second += cost_visitor.loads;
      }

      // Estimating cost when reductions are involved
      // TODO: This assumes that the entire reduction of each
      // update definition is evaluated to compute each value of
      // the function.
      if (!kv.second.is_pure()) {
        for (const UpdateDefinition &u: kv.second.updates()) {

          int64_t ops = 1;
          int64_t loads = 0;
          for (auto &e: u.values) {
            ExprCost cost_visitor;
            e.accept(&cost_visitor);
            ops += cost_visitor.ops;
            loads += cost_visitor.loads;
          }

          for (auto &arg: u.args) {
            ExprCost cost_visitor;
            arg.accept(&cost_visitor);
            ops += cost_visitor.ops;
            loads += cost_visitor.loads;
          }

          if (u.domain.defined()) {
            Box b;
            for (auto &rvar: u.domain.domain()) {
              b.push_back(Interval(simplify(rvar.min),
                                   simplify(rvar.min + rvar.extent - 1)));
            }
            int64_t area = box_area(b);
            if (area != -1) {
              func_cost[kv.first].first += ops * area;
              func_cost[kv.first].second += loads * area;
            } else {
              func_cost[kv.first].first = -1;
              func_cost[kv.first].second = -1;
              debug(0) << "Warning: could not determine the bounds of\
                           the rdom in function " << kv.first << '\n';
            }
          }
        }
      }
    }
  }
};

struct DependenceAnalysis {

  const map<string, Function> &env;
  const FuncValueBounds &func_val_bounds;

  // TODO: Build a cache for bounds queries

  DependenceAnalysis(map<string, Function> &_env,
                     const FuncValueBounds &_func_val_bounds):
                     env(_env), func_val_bounds(_func_val_bounds) {}

  void simplify_box(Box& b) {
    for (unsigned int i = 0; i < b.size(); i++) {
      b[i].min = simplify(b[i].min);
      b[i].max = simplify(b[i].max);
    }
  }

  /* Compute the regions of producers required to compute a region of the function
     'f' given concrete sizes of the tile in each dimension. */
  map<string, Box> regions_required(Function f,
                                    const vector<Interval> &conc_bounds) {

    map<string, Box> regions;
    // Add the function and its region to the queue
    deque< pair<Function, vector<Interval> > > f_queue;
    f_queue.push_back(make_pair(f, conc_bounds));

    // Recursively compute the regions required
    while(!f_queue.empty()) {

      Function curr_f = f_queue.front().first;
      vector<Interval> curr_bounds = f_queue.front().second;
      f_queue.pop_front();

      for (auto &val: curr_f.values()) {
        map<string, Box> curr_regions;
        Scope<Interval> curr_scope;
        int interval_index = 0;

        for (auto& arg: curr_f.args()) {
          Interval simple_bounds = Interval(simplify(curr_bounds[interval_index].min),
                                            simplify(curr_bounds[interval_index].max));
          curr_scope.push(arg, simple_bounds);
          interval_index++;
        }

        curr_regions = boxes_required(val, curr_scope, func_val_bounds);
        for (auto& reg: curr_regions) {
          // merge region with an existing region for the function in
          // the global map
          if (regions.find(reg.first) == regions.end())
            regions[reg.first] = reg.second;
          else
            merge_boxes(regions[reg.first], reg.second);

          if (env.find(reg.first) != env.end())
            f_queue.push_back(make_pair(env.at(reg.first),
                                        reg.second.bounds));
        }
      }

      // TODO: Check if this handling of updates is correct
      for (auto &update: curr_f.updates()) {
        for (auto &val: update.values) {
          map<string, Box> curr_regions;
          Scope<Interval> curr_scope;
          int interval_index = 0;
          vector<Expr> exprs;
          exprs.push_back(val);
          for (auto &arg: update.args) {
            Interval simple_bounds = Interval(simplify(curr_bounds[interval_index].min),
                                              simplify(curr_bounds[interval_index].max));
            // Check for a pure variable
            const Variable *v = arg.as<Variable>();
            if (!v) {
              // Need to evaluate boxes required on args that are not pure
              // for potenial calls to other functions
              exprs.push_back(arg);
            } else {
              curr_scope.push(v->name, simple_bounds);
            }
            interval_index++;
          }

          if (update.domain.defined()) {
            for (auto &rvar: update.domain.domain()) {
              Interval simple_bounds = Interval(rvar.min,
                                                rvar.min + rvar.extent - 1);
              curr_scope.push(rvar.var, simple_bounds);
            }
          }

          for (auto &e: exprs) {
            curr_regions = boxes_required(e, curr_scope, func_val_bounds);
            for (auto& reg: curr_regions) {
              // Merge region with an existing region for the function in
              // the global map
              if(reg.first != curr_f.name()) {
                if (regions.find(reg.first) == regions.end())
                  regions[reg.first] = reg.second;
                else
                  merge_boxes(regions[reg.first], reg.second);

                if (env.find(reg.first) != env.end())
                  f_queue.push_back(make_pair(env.at(reg.first),
                                              reg.second.bounds));
              }
            }
          }
        }
      }
    }

    // Simplify
    map<string, Box> concrete_regions;

    for (auto &f_reg : regions) {
      simplify_box(f_reg.second);

      Box concrete_box;
      for (unsigned int i = 0; i < f_reg.second.size(); i++) {
        Expr lower = f_reg.second[i].min;
        Expr upper = f_reg.second[i].max;

        // Use the estimates if the lower and upper bounds cannot be
        // determined
        if (!lower.as<IntImm>()) {
          const Function &curr_f = env.at(f_reg.first);
          for (auto &b: curr_f.schedule().estimates()) {
            unsigned int num_pure_args = curr_f.args().size();
            if (i < num_pure_args && b.var == curr_f.args()[i])
              lower = Expr(b.min.as<IntImm>()->value);
          }
        }

        if (!upper.as<IntImm>()) {
          const Function &curr_f = env.at(f_reg.first);
          for (auto &b: curr_f.schedule().estimates()) {
            unsigned int num_pure_args = curr_f.args().size();
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

  /* Compute the redundant regions computed while computing a tile of the function
     'f' given sizes of the tile in each dimension. */
  map<string, Box> redundant_regions(Function f, int dir,
                                     const vector<Interval> &conc_bounds) {

    map<string, Box> regions = regions_required(f, conc_bounds);

    vector<Interval> shifted_bounds;
    int num_pure_args = f.args().size();
    for (int arg = 0; arg < num_pure_args; arg++) {
      if (dir == arg) {
        Expr len = conc_bounds[arg].max - conc_bounds[arg].min + 1;
        Interval bound = Interval(conc_bounds[arg].min + len,
                                  conc_bounds[arg].max + len);
        shifted_bounds.push_back(bound);
      }
      else
        shifted_bounds.push_back(conc_bounds[arg]);
    }

    map<string, Box> regions_shifted = regions_required(f, shifted_bounds);

    map<string, Box> overalps;
    for (auto& reg: regions) {
      if (regions_shifted.find(reg.first) == regions.end()) {
        // It will be interesting to log cases where this actually
        // happens
        continue;
      } else {
        Box b = reg.second;
        Box b_shifted = regions_shifted[reg.first];
        // The boxes should be of the same size
        assert(b.size() == b_shifted.size());
        // The box used makes things complicated ignoring it for now
        Box b_intersect;
        for (unsigned int i = 0 ; i < b.size(); i++)
          b_intersect.push_back(interval_intersect(b[i], b_shifted[i]));
        // A function should appear once in the regions and therefore cannot
        // already be present in the overlaps map
        assert(overalps.find(reg.first) == overalps.end());
        overalps[reg.first] = b_intersect;
      }
    }
    // Simplify
    for (auto &f : overalps)
      simplify_box(f.second);

    return overalps;
  }


  map<string, Box>
      concrete_dep_regions(string name, vector<Interval> &bounds) {
        return regions_required(env.at(name), bounds);
      }

  vector< map<string, Box> >
      concrete_overlap_regions(string name, vector<Interval> &bounds) {

        vector< map<string, Box> > conc_overlaps;
        size_t num_args = env.at(name).args().size();

        for (size_t dir = 0; dir < num_args; dir++) {
          map<string, Box> conc_reg =
              redundant_regions(env.at(name), dir, bounds);
          conc_overlaps.push_back(conc_reg);
        }
        return conc_overlaps;
      }
};

void disp_regions(map<string, Box> &regions) {
    for (auto& reg: regions) {
        debug(0) << reg.first;
        debug(0) << reg.second;
        debug(0) << "\n";
    }
}

map<string, Box> get_pipeline_bounds(DependenceAnalysis &analy,
                                     const vector<Function> &outputs) {
  map<string, Box> pipeline_bounds;

  for (auto &out: outputs) {
    vector<Interval> bounds;
    vector<string> vars = out.args();
    for (size_t i = 0; i < vars.size(); i++) {
      for (auto &b: out.schedule().estimates())
        if (b.var == vars[i]) {
          Interval I = Interval(b.min, simplify(b.min + b.extent - 1));
          bounds.push_back(I);
        }
    }

    map<string, Box> regions =
        analy.concrete_dep_regions(out.name(), bounds);

    // Add the output region to the pipeline bounds as well
    regions[out.name()] = bounds;

    for (auto& reg: regions) {
      // Merge region with an existing region for the function in
      // the global map
      if (pipeline_bounds.find(reg.first) == pipeline_bounds.end())
        pipeline_bounds[reg.first] = reg.second;
      else
        merge_boxes(pipeline_bounds[reg.first], reg.second);
    }
  }

  return pipeline_bounds;
}

struct Partitioner {

  struct InlineChoice {
    // InlineChoice encodes the choice of the prod_group being inlined
    // into the cons_group
    string prod_group;
    string cons_group;
    InlineChoice(string _prod_group, string _cons_group) :
      prod_group(_prod_group), cons_group(_cons_group) {}
  };

  struct FusionChoice {
    // FusionChoice encodes the choice of the prod_group being merged with
    // the cons_group at the granularity of the tile given by tile_sizes
    string prod_group;
    string cons_group;
    // Tile sizes along the output of the consumer group
    map<string, int> tile_sizes;

    FusionChoice(string _prod_group, string _cons_group,
                 map<string, int>& _tile_sizes) :
      prod_group(_prod_group), cons_group(_cons_group),
      tile_sizes(_tile_sizes) {}

    friend std::ostream& operator <<(std::ostream& stream,
                                     const FusionChoice& choice) {

      stream << "Choice:" << choice.prod_group << "->"
                          << choice.cons_group << '\n';

      stream << "Tile sizes:" << "[";
      for (auto &s: choice.tile_sizes) {
        stream << "(" << s.first << "," <<  s.second << ")";
      }
      stream << "]" << '\n';

      return stream;
    }

  };

  map<pair<string, string>, int64_t> fusion_cache;

  struct Group {
    // The output function representing the group
    string output;
    // All the functions that belong to the group
    vector<Function> members;

    // Estimate of arithmetic cost
    int64_t arith_cost;
    // Estimate of accesses to slow memory
    int64_t mem_cost;
    // Estimate of the parallelism
    int64_t parallelism;
    // Indicator to say if the cost of the group has been
    // evaluated
    bool cost_evaluated;

    // Reuse along dimensions of the group members
    map<string, map<string, int64_t> > reuse;

    // Schedule information
    // All the members of the group which are inlined
    set<string> inlined;
    // For now this is just the tile sizes since the we only tile the output of
    // the group and compute all the members of the group at that granularity
    map<string, int> tile_sizes;

    Group(string _output, vector<Function> _members):
      output(_output), members(_members) {
        arith_cost = -1;
        mem_cost = -1;
        parallelism = -1;
        cost_evaluated = false;
      }

    friend std::ostream& operator <<(std::ostream& stream, const Group& g) {

      stream << "Output:" << g.output << '\n';
      stream << "Memebers:" << '[';
      for (auto &m: g.members) {
        stream << m.name() << ",";
      }
      stream << "]" << '\n';

      stream << "Inlined:" << '[';
      for (auto &in: g.inlined) {
        stream << in << ",";
      }
      stream << "]" << '\n';

      stream << "Tile sizes:" << "[";
      for (auto &s: g.tile_sizes) {
        stream << "(" << s.first << "," <<  s.second << ")";
      }
      stream << "]" << '\n';

      stream << "Arithmetic cost:" << g.arith_cost  << '\n';
      stream << "Memory cost:" << g.mem_cost  << '\n';
      stream << "Parallelism:" << g.parallelism  << '\n';
      return stream;
    }
  };

  map<string, Group> groups;

  // Levels that are targetted by the grouping algorithm
  enum Level {INLINE, FAST_MEM};

  map<string, Box> &pipeline_bounds;
  MachineParams &arch_params;
  DependenceAnalysis &analy;
  //map<string, pair<long long, long long> > &func_cost;
  const vector<Function> &outputs;

  //map<string, float > input_reuse;

  map<string, set<string> > children;
  void disp_children(map<string, set<string> > &children) {
    for (auto &f: children) {
      debug(0) << f.first <<  ": [";
      for (auto &c: f.second)
        debug(0) << c << ",";
      debug(0) << "]" << '\n';
    }
  }

  // These are pre-computed values that are used throughout the grouping
  // TODO: see what is required and purge the rest
  map<string, vector<int> > func_pure_dim_estimates;
  map<string, map<string, int> > func_dim_estimates;
  map<string, int64_t > func_op;
  map<string, int64_t > func_size;

  bool gpu_schedule;

  Partitioner(map<string, Box> &_pipeline_bounds, MachineParams &_arch_params,
              DependenceAnalysis &_analy, const vector<Function> &_outputs,
              bool _gpu_schedule):
              pipeline_bounds(_pipeline_bounds), arch_params(_arch_params),
              analy(_analy), outputs(_outputs), gpu_schedule(_gpu_schedule) {

      // Place each function in its own group
      for (auto &f: analy.env) {
        vector<Function> members;
        members.push_back(f.second);
        Group g(f.first, members);
        groups.insert(make_pair(f.first, g));
      }

      // Find consumers of each function relate groups with their children
      for (auto &f: analy.env) {
        map<string, Function> calls = find_direct_calls(f.second);
        for (auto &c: calls)
          if (c.first != f.first)
            children[c.first].insert(f.first);
      }

      disp_children(children);

      //TODO: Any preprocess inlining should go here and they
      //should be added to the corresponding group as inlined
      //members

      // Precompute the arithmetic and load costs for each function
      // in the pipeline
    }

  void merge_groups(string cand_group, string child_group) {
    assert(groups.find(child_group) != groups.end());
    vector<Function> cand_funcs = groups.at(cand_group).members;

    groups.erase(cand_group);

    vector<Function> &child_members = groups.at(child_group).members;
    child_members.insert(child_members.end(),
                         cand_funcs.begin(), cand_funcs.end());

    // Update the children mapping
    children.erase(cand_group);
    for (auto &f: children) {
      set<string> &cons = f.second;
      if (cons.find(cand_group) != cons.end()) {
        cons.erase(cand_group);
        cons.insert(child_group);
      }
    }
  }

  void merge_group_into_all_children(string cand_group) {

    set<string> cand_group_children = children[cand_group];
    for (auto &cg: cand_group_children) {
      assert(groups.find(cg) != groups.end());
      vector<Function> cand_funcs = groups.at(cand_group).members;

      vector<Function> &cg_members = groups.at(cg).members;
      cg_members.insert(cg_members.end(),
                        cand_funcs.begin(), cand_funcs.end());
    }

    groups.erase(cand_group);

    // Update the children mapping
    children.erase(cand_group);
    for (auto &f: children) {
      set<string> &cons = f.second;
      if (cons.find(cand_group) != cons.end()) {
        cons.erase(cand_group);
        cons.insert(cand_group_children.begin(),
                    cand_group_children.end());
      }
    }
  }

  void disp_grouping() {
    for (auto& g: groups) {
      debug(0) << "Group " <<  g.first  << " : [" ;
      debug(0) << g.second;
      debug(0) << "]" << '\n';
    }
  }

  int64_t evaluate_choice(FusionChoice &choice);
  int64_t evaluate_choice(InlineChoice &choice);

  void evaluate_group_cost(Group &g);
  vector<Interval> get_bounds_from_tile_sizes(string func,
                                              map<string, int> &tile_sizes);
  void group(Partitioner::Level level);
  pair<vector<InlineChoice>, int64_t>
      choose_candidate_fuse_inline(const vector< pair<string, string > > &cand_pairs);
  pair<FusionChoice, int64_t>
      choose_candidate_fuse_fast_mem(const vector< pair<string, string > > &cand_pairs);
/*
  Option choose_candidate(const vector< pair<string, string > > &cand_pairs);
  void clear_schedules_fast_mem();
  void initialize_groups_fast_mem();
  void initialize_groups_inline();
  void update_function_costs();
  void tile_for_input_locality(bool init_pipeline_reuse = false);
  vector<float> get_input_reuse(Function f, vector<string> &inputs);
  pair<float, float> evaluate_reuse(string, vector<string> &group_inputs,
                                    vector<int> &tile_sizes, bool unit_tile);
*/
};
//TODO: This should not be candidate pairs
pair<vector<Partitioner::InlineChoice>, int64_t>
Partitioner::choose_candidate_fuse_inline(
    const vector< pair<string, string> > &cand_pairs) {

  pair<vector<Partitioner::InlineChoice>, int64_t> best;
  best.second = -1;
  for (auto &p: cand_pairs) {
    // Compute the aggregate benefit for inlining into all the children
    int64_t overall_benefit = 0;
    vector<Partitioner::InlineChoice> choices;

    for (auto &c: children[p.first]) {
      int64_t benefit = 0;
      // Get the output function of the child group
      Function output = analy.env.at(c);

      InlineChoice cand_choice(p.first, c);
    
      // Check if the pair has been evaluated for inline fusion before
      pair<string, string> key = make_pair(p.first, c);
      if (fusion_cache.find(key) != fusion_cache.end()) {

        best.second = fusion_cache.at(key);

      } else {
        benefit = evaluate_choice(cand_choice);
        // Cache the result of the evaluation for the pair
        fusion_cache.insert(make_pair(key, benefit));
      }

      // Conservative strategy that only goes ahead with the fusion
      // if all the fusions into the consumers are beneficial
      // TODO: Create a test where this assumption breaks
      if (benefit < 0) {
        overall_benefit = -1;
        break;
      } else {
        choices.push_back(cand_choice);
        overall_benefit += benefit;
      }
    }

    if (best.second < overall_benefit) {
      best.first = choices;
      best.second = overall_benefit;
    }
  }
  return best;
}

pair<Partitioner::FusionChoice, int64_t>
Partitioner::choose_candidate_fuse_fast_mem(
    const vector< pair<string, string> > &cand_pairs) {

  map<string, int> tile_sizes;
  FusionChoice c("", "", tile_sizes);
  return make_pair(c, 0);
}

void Partitioner::group(Partitioner::Level level) {
    // Partition the pipeline by iteratively merging groups until a fixpoint
    bool fixpoint = false;
    while(!fixpoint) {
        fixpoint = true;
        vector< pair<string, string> > cand;
        for (auto &g: groups) {

            bool is_output = false;
            for (auto &f: outputs) {
              if (g.first == f.name())
                is_output = true;
            }

            if (is_output)
                continue;

            if (children.find(g.first) != children.end()) {
                int num_children = children[g.first].size();
                // Find all the groups which have a single child
                if (num_children == 1 && level == Partitioner::FAST_MEM) {
                    cand.push_back(make_pair(g.first,
                                             *children[g.first].begin()));
                } else if(num_children > 0  && level == Partitioner::INLINE) {
                    cand.push_back(make_pair(g.first, ""));
                }
            }
        }

        debug(0) << "Current grouping candidates:" << '\n';
        for (auto &p: cand) {
          debug(0) << "[" << p.first << "," <<  p.second << "]" << '\n';
        }

        vector<pair<string, string> > invalid_keys;
        if (level == Partitioner::INLINE) {
            pair<vector<InlineChoice>, int64_t> best;
            best = choose_candidate_fuse_inline(cand);
            if (best.second >= 0) {
                string prod = best.first[0].prod_group;

                for (auto &o: best.first)
                    internal_assert(o.prod_group == prod);

                // Mark the entries of the fusion cache that need to be
                // invalidated
                for (auto &c: children[prod]) {
                    for (auto& choice: fusion_cache) {
                        if (choice.first.first == c ||
                                choice.first.second == c)
                            invalid_keys.push_back(choice.first);
                    }
                }
                merge_group_into_all_children(prod);
                fixpoint = false;
            }

        } else {
            pair<FusionChoice, int64_t> best
                = choose_candidate_fuse_fast_mem(cand);
            if (best.second >= 0) {

                // Mark the entries of the fusion cache that need to be
                // invalidated
                for (auto& choice: fusion_cache) {
                    if (choice.first.second == best.first.cons_group
                            || choice.first.first == best.first.cons_group)
                        invalid_keys.push_back(choice.first);
                }

                merge_groups(best.first.prod_group, best.first.cons_group);
                fixpoint = false;
            }
        }

        // Invalidate the fusion cache
        for (auto& key: invalid_keys)
            fusion_cache.erase(key);
    }
}

vector<Interval> Partitioner::get_bounds_from_tile_sizes(string func,
                                                         map<string, int> &tile_sizes) {
  vector<Interval> bounds;
  const vector<string> &args = analy.env.at(func).args();

  for (size_t i = 0; i < args.size(); i++) {
    if (tile_sizes.find(args[i]) != tile_sizes.end()) {
      int size = tile_sizes.at(args[i]);
      // Check if the bounds allow for tiling with the given tile size
      // i.e., ensure atleast 2 tiles
      int extent = get_extent(pipeline_bounds.at(func)[i]);
      if (extent >= 2 * size) {
        // TODO: Maybe shift this to the center of the pipeline bound
        bounds.push_back(Interval(0, size - 1));
      }
      else {
        // If the dimension is too small do not tile it and set the
        // extent of the bounds to that of the dimension estimate
        bounds.push_back(pipeline_bounds.at(func)[i]);
      }
    }
    else {
      bounds.push_back(pipeline_bounds.at(func)[i]);
    }
  }

  return bounds;
}

void Partitioner::evaluate_group_cost(Group &g) {
  // Estimating the number of accesses to slow memory

  // 1) Assume all loads are a miss if the working set does not fit
  // in cache. This ignores any locality that results from the
  // iteration order. This is pretty aggresive in estimating the benefit
  // of fusion.
  //
  // 2) Assume that the intermediates are loaded only once even if
  // the do not fit in cache. It is a pretty good model for pipelines
  // which are streaming in nature. This gives a conservative estimate
  // of fusion benefit and does not accurately capture scenarios where
  // there is significant reuse.
  //
  // The actual number of accesses will inbetween 2) and 1) for now
  // going with model 1).
  //
  // TODO: See if the model needs to be refined further to account
  // for spatial locality and iteration order.

  set<string> group_inputs;

  for(auto &f: g.members) {
    FindAllCalls find;
    analy.env.at(f.name()).accept(&find);
    //for(auto &c: find.calls) {
    //  if (std::find(g.members.begin(), g.members.end(), c) == g.members.end())
    //    group_inputs.insert(c);
    //}
  }

  // Count the number of tiles
  uint64_t estimate_tiles = 1;
  uint64_t num_ele_per_tile = 1;

  const vector<string> &args = analy.env.at(g.output).args();

  for (size_t i = 0; i < args.size(); i++) {
    if (g.tile_sizes.find(args[i]) != g.tile_sizes.end()) {
      int size = g.tile_sizes.at(args[i]);
      int extent = get_extent(pipeline_bounds.at(g.output)[i]);
      estimate_tiles *= std::ceil((float)extent/size);
      num_ele_per_tile *= size;
    }
  }

  // Determining the size of the intermediates
  vector<Interval> bounds = get_bounds_from_tile_sizes(g.output,
                                                       g.tile_sizes);
  map<string, Box> conc_reg = analy.concrete_dep_regions(g.output, bounds);

  // disp_regions(conc_reg);

  // TODO: Edit the text on the cost model
  // Cost model

  // We currently assume a two level memory model. The fast_mem_size field in
  // the arch parameters gives the size of the fast memory. Additionally, the
  // ratio of load from fast memory vs slow memory is encoded in the machine
  // parameters.

  // We compute the size of the intermediate buffers that are required to
  // compute the output of the group.

  // inter_s = size of the intermediates in the fused group
  // M = fast memory size
  // s_c = the cost of loading from slow memory
  // f_c = the cost of loading from fast memory
  // op_c = the cost of computing an op

  // The benefit of an option is the reduction in the number of operations
  // that read/write to slow memory and the benefit is calculated per tile
  //
  // if inter_s fits in fast memory then
  //    inter_s * s_c - (inter_s * f_c + (redundant_ops) * op_c)
  //    => inter_s * (s_c - f_c) - (redundant_ops) * op_c
  // else
  //    hit = max(2M - inter_s, 0) assuming LRU
  //    inter_s * s_c - (hit * f_c + (inter_s - hit) * s_c + (redundant_ops)
  //                     * op_c)
  //    => hit * (s_c - f_c) - (redundant_ops) * op_c

  map<string, Box> mem_reg;

  // TODO: Do not count inlines while accounting for intermediate
  // storage when grouping for fast mem
  for (auto &f: g.members)
    mem_reg[f.name()] = conc_reg[f.name()];

  mem_reg[g.output] = Box(bounds);
}


int64_t Partitioner::evaluate_choice(InlineChoice &choice) {
  return 0;
}

int64_t Partitioner::evaluate_choice(FusionChoice &choice) {
  //disp_option(opt);

  map<string, Box> conc_reg;

  // Create a group that reflects the fusion choice and evaluate
  // the cost of the group
  Group prod_group = groups.at(choice.prod_group);
  Group cons_group = groups.at(choice.cons_group);

  vector<Function> fused_members;
  for(auto &f: prod_group.members)
    fused_members.push_back(f);
  for(auto &f: cons_group.members)
    fused_members.push_back(f);

  Group fused_group(cons_group.output, fused_members);

  for(auto &f: prod_group.inlined)
    fused_group.inlined.insert(f);
  for(auto &f: cons_group.inlined)
    cons_group.inlined.insert(f);

  fused_group.tile_sizes = choice.tile_sizes;

  // Compare the cost with the costs of the groups without fusion
  evaluate_group_cost(prod_group);
  evaluate_group_cost(cons_group);
  evaluate_group_cost(fused_group);

  // Return the overall benefit of the choice
  // TODO: Use the arch params to compute total work
  return 0;
}

void generate_schedules(const vector<Function> &outputs,
                        const Target &target) {
  // Compute an environment
  map<string, Function> env;
  for (Function f : outputs) {
    map<string, Function> more_funcs = find_transitive_calls(f);
    env.insert(more_funcs.begin(), more_funcs.end());
  }

  // Compute a realization order
  vector<string> order = realization_order(outputs, env);

  // Set the schedule defaults for each funtion in the environment
  set_schedule_defaults(env);

  // Compute the expression costs for each function in the pipeline

  // Dependence analysis to compute all the regions of upstream functions
  // required to compute a region of the function

  FuncValueBounds func_val_bounds = compute_function_value_bounds(order, env);

  bool estimates_avail = check_estimates_on_outputs(outputs);

  // Inform the user that estimates of output sizes were not available on
  // the outputs of the pipeline.
  user_assert(estimates_avail) << "Please provide estimates for each \
                               dimension of the pipeline output functions.";

  map<string, vector<string> > update_args;
  set<string> reductions;
  DependenceAnalysis analy(env, func_val_bounds);

  // Show bounds of all the functions in the pipeline given estimates
  // on outputs. Also report fuctions where the bounds could not be inferred.
  map<string, Box> pipeline_bounds = get_pipeline_bounds(analy, outputs);
  disp_regions(pipeline_bounds);

  // Initialize the cost model
  // TODO: Build a class which encapsulates the cost model
  // TODO: Arithmetic cost model and functions which help in computing
  // foot prints go here

  // TODO: Partitioner which is capable of auto scheduling hierarchically
  // TODO: Auto scheduler modes
  // O1 Does not introduce any redundant compute but performs basic fusion
  // O2 No redundant compute basic fusion and reordering
  // O3 Trades-offs redundant work for enhancing locality and parallelism

  // TODO: Realize the generated schedule
  // CPU

  // TODO: Update definitions
  // TODO: Boundary conditions

  // TODO: Realize the generated schedule
  // GPU
  // ...

}

}
}
