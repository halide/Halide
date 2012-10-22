#include "Rehydrate.h"
#include "FuncContents.h"
#include "ExprContents.h"
#include "Util.h"

#include <map>
#include <vector>
#include <set>
using std::map;
using std::vector;
using std::pair;
using std::set;

#include <iostream>
using std::cerr;
using std::endl;

namespace Halide {

    ML_FUNC1(deserializeEnv)
    ML_FUNC1(getEnvDefinitions)
    ML_FUNC1(typeOfExpr)
    ML_FUNC1(varsInExpr)
    ML_FUNC1(callsInExpr)

    ML_FUNC1(callTypeIsFunc)
    ML_FUNC1(callTypeIsExtern)
    ML_FUNC1(callTypeIsImage)

    // Mirror helpers for unpacking `definitions` from the deserialized environment.
    // Yes, this is ugly at this point. Better ideas (short of adding dozens of
    // accessor callback helpers to halide_cpp.ml) are welcome.
    struct Definition {
        Definition() {}

        Definition(MLVal d) : name(string(d[0])), ret_t(d[2]), body(d[3]) {
            for (MLVal list = d[1]; !listEmpty(list); list = listTail(list)) {
                MLVal arg = listHead(list);
                args.push_back(make_pair(string(arg[1]), arg[0]));
            }
        }

        struct Body {
            Body() {}

            Body(MLVal b) {
                if (functionIsPure(b)) {
                    ty = Pure;
                    rhs = getPureBody(b);
                } else {
                    assert(functionIsReduce(b));
                    ty = Reduce;
                    MLVal red = getReduceBody(b);
                    rhs = red[0];
                    updateLoc = red[1];
                    updateFunc = string(red[2]);
                    dom = red[3];
                }
            }

            enum {
                Pure,
                Reduce
            } ty;

            MLVal rhs; // pure or initializer expr

            // if reduce:
            MLVal updateLoc;
            string updateFunc;
            MLVal dom;
        };

        string name;
        vector<pair<string, Type> > args;
        Type ret_t;
        Body body;

        bool isReduce() { return body.ty == Body::Reduce; }
    };

    void testArray(Func f) {
        string sexp = f.serialize();
        cerr << sexp << endl;
        rehydrate(sexp, f.name());
    }

    Func rehydrateFunc(map<string, Definition>& defs,
                       map<string, Func>& env,
                       const string func);

    Expr rehydrateExpr(map<string, Definition>& defs,
                       map<string, Func>& env,
                       set<string> curArgs,
                       MLVal expr)
    {
        Expr e(expr, typeOfExpr(expr));

        cerr << "rehydrateExpr: " << e.pretty() << endl;

        //
        // Track dependences
        //

        // Unpack calls first. With the UniformImages, we can (mostly) disambiguate
        // buffer dims from standalone uniforms
        for (MLVal list = callsInExpr(expr); !listEmpty(list); list = listTail(list)) {
            MLVal call = listHead(list);
            string name = call[0];
            Type ret = call[1][1];
            if (callTypeIsFunc(call[1][0])) {
                Func f = rehydrateFunc(defs, env, name);
                e.child(FuncRef(f));
            } else if (callTypeIsImage(call[1][0])) {
                assert (name[0] == '.'); // should be an absolute name
                name = name.substr(1);   // chop off the leading '.'
                int dims = listLength(call[1][2]); // count number of args
                e.child(UniformImage(ret, dims, name));
            }
        }

        // TODO: we need some more distinctive marker for image dimension references
        // e.g. we could use a dedicated character, which, if present, implies that a 
        // uniform refers to a buffer_t field, and otherwise it is a scalar uniform.
        for (MLVal list = varsInExpr(expr); !listEmpty(list); list = listTail(list)) {
            MLVal var = listHead(list);
            string name = var[0];
            Type t = var[1];
            if (curArgs.count(name)) {
                // This is a simple free variable
                assert (name[0] != '.');
                e.child(Var(name));
                cerr << "  var: " << name << endl;
            } else {
                // This is a uniform
                assert (name[0] == '.'); // should be an absolute name
                name = name.substr(1);   // chop off the leading '.'

                string root_name = name.substr(0, name.find('.'));
                cerr << " -Uniform root_name: " << root_name << endl;
                bool is_image = false;
                for (int i = 0; i < e.uniformImages().size(); i++) {
                    cerr << "    -check uniformImage " << e.uniformImages()[i].name() << endl;
                    if (e.uniformImages()[i].name() == root_name) {
                        is_image = true;
                        break;
                    }
                }
                if (is_image) {

                } else {
                    cerr << "  uniform: " << name << endl;
                    e.child(DynUniform(t, name));
                }
            }
        }

        return e;
    }

    Func rehydrateFunc(map<string,
                       Definition>& defs,
                       map<string,
                       Func>& env,
                       const string func)
    {
        // If we've already rehydrated this, return it
        if (env.count(func)) return env[func];

        cerr << "Rehydrating " << func << endl;

        // Build a new FuncContents from the definition
        Definition& def = defs[func];
        FuncContents *c = new FuncContents(def.name, def.ret_t);
        assert(!def.isReduce()); // TODO: this isn't handled yet
        
        set<string> args;
        for (size_t i = 0; i < def.args.size(); i++) {
            string arg = def.args[i].first;
            c->args.push_back(Var(arg));
            args.insert(arg);
        }

        // Rehydrate the rhs Expr
        c->rhs = rehydrateExpr(defs, env, args, def.body.rhs);

        // Add it to the environment
        env[func] = c->toFunc();

        return env[func];
    }

    Func rehydrate(const string sexp, const string rootFunc) {
        map<string, Func> env;

        map<string, Definition> defs;
        for (MLVal list = getEnvDefinitions(deserializeEnv(sexp));
             !listEmpty(list); list = listTail(list)) {
            Definition def(listHead(list));
            defs[def.name] = def;
            cerr << def.name << " is " << (def.isReduce() ? "" : "not ") << "reduce" << endl;
        }

        return rehydrateFunc(defs, env, rootFunc);
    }

}
