#include "FImage.h"

// declare the functions that live on the ml side

ML_FUNC1(makeIntImm);
ML_FUNC2(makeAdd);
ML_FUNC1(doPrint);
ML_FUNC1(makeVar);
ML_FUNC2(doRun); // statement, arglist pointer
ML_FUNC2(makeLoad); // buffer id, idx
ML_FUNC3(makeStore); // value, buffer id, idx

namespace FImage {
    Expr Add(const Expr &a, const Expr &b) {
        return Expr(makeAdd(a.val, b.val));
    }

    Expr IntImm(int a) {
        return Expr(makeIntImm(MLVal::fromInt(a)));
    }

    /*
    Expr Var(const char *a) {
        return Expr(makeVar(MLVal::fromString(a)));
    }
    */

    Var::Var() : Expr(makeVar(MLVal::fromString("var"))) {
    }

    Var::Var(const char *a) : Expr(makeVar(MLVal::fromString(a))) {
    }

    void print(const Expr &a) {
        doPrint(a.val);
    }
    
    Expr::Expr(int x) : val(makeIntImm(MLVal::fromInt(x))) {
    }

    Expr::Expr(MLVal v) : val(v) {
    }

    Expr Expr::operator+(const Expr &b) {
        return Add(*this, b);
    }

    void run(const Expr &stmt, void *args) {
        doRun(stmt.val, MLVal::fromPointer(args));
    }

    Expr Load(int buf, const Expr &idx) {
        return makeLoad(MLVal::fromInt(buf), idx.val);
    }

    Expr Store(const Expr &val, int buf, const Expr &idx) {
        return makeStore(val.val, MLVal::fromInt(buf), idx.val);
    }

}
