#ifndef FUNC_H
#define FUNC_H

#include "IR.h"
#include <iostream>

namespace HalideInternal {

    struct Schedule {
        string store_level, compute_level;

        struct Split {
            string old_var, outer, inner;
            Expr factor;
        };
        vector<Split> splits;

        struct Dim {
            string var;
            For::ForType for_type;
        };
        vector<Dim> dims;
    };

    struct FuncContents {
        mutable int ref_count;
        string name;
        vector<string> args;
        Expr value;
        Schedule schedule;
        // TODO: reduction step lhs, rhs, and schedule
    };

    class FuncRef {
        
    };

    class Func : public IntrusivePtr<FuncContents> {
    public:        
        Stmt lower(const map<string, Func> &env);
        static void test();

        const string &name() const {return ptr->name;}
        const vector<string> &args() const {return ptr->args;}
        Expr value() const {return ptr->value;}
        const Schedule &schedule() const {return ptr->schedule;}

        void define(string name, const vector<string> &args, Expr value) {
            assert(!defined() && "Function is already defined");
            ptr = new FuncContents;
            ptr->name = name;
            ptr->value = value;
            ptr->args = args;

            for (size_t i = 0; i < args.size(); i++) {
                Schedule::Dim d = {args[i], For::Serial};
                ptr->schedule.dims.push_back(d);
            }


        }

        Func &split(string old, string outer, string inner, Expr factor) {
            // Replace the old dimension with the new dimensions in the dims list
            bool found = false;
            vector<Schedule::Dim> &dims = ptr->schedule.dims;
            for (size_t i = 0; (!found) && i < dims.size(); i++) {
                if (dims[i].var == old) {
                    found = true;
                    dims[i].var = inner;
                    dims.push_back(dims[dims.size()-1]);
                    for (size_t j = dims.size(); j > i+1; j--) {
                        dims[j-1] = dims[j-2];
                    }
                    dims[i+1].var = outer;
                }
            }

            assert(found && "Could not find dimension in argument list for function");

            // Add the split to the splits list
            Schedule::Split split = {old, outer, inner, factor};
            ptr->schedule.splits.push_back(split);

            return *this;
        }

    private:
        void set_dim_type(string var, For::ForType t) {
            bool found = false;
            vector<Schedule::Dim> &dims = ptr->schedule.dims;
            for (size_t i = 0; (!found) && i < dims.size(); i++) {
                if (dims[i].var == var) {
                    found = true;
                    dims[i].for_type = t;
                }
            }

            assert(found && "Could not find dimension in argument list for function");
        }

    public:
        Func &parallel(string var) {
            set_dim_type(var, For::Parallel);
            return *this;
        }

        Func &vectorize(string var) {
            set_dim_type(var, For::Vectorized);
            return *this;
        }

        Func &unroll(string var) {
            set_dim_type(var, For::Unrolled);
            return *this;
        }

        Func &chunk(string store, string compute) {
            ptr->schedule.store_level = store;
            ptr->schedule.compute_level = compute;
            return *this;
        }
        
    };


}

#endif
