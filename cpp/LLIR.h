#ifndef _LLIR_H
#define _LLIR_H

#include <vector>
#include <string>
using namespace std;

#ifndef _MSC_VER
#include <tr1/unordered_set>
#include <tr1/memory>
using namespace std::tr1;
#else
#include <memory>
#include <unordered_set>
#endif

namespace LLIR {

class Stmt {
public:
    typedef shared_ptr<Stmt> Ptr;
    virtual ~Stmt() {};
};

class Type {
public:
    typedef enum { Int, UInt, Float } T;
    Type(T t, int w) : t(t), width(w) {}
    T t;
    int width; // bit-width
    // TODO(jrk) store these more usefully in a map?
    // TODO(jrk) not legal to declare these within the type in C++ -- what's the LLVM style here?
    static const Type Bool = Type(UInt, 1); // separate bool type, or is UInt1 convention fine?
    static const Type U8   = Type(UInt, 8);
    static const Type U16  = Type(UInt, 16);
    static const Type U32  = Type(UInt, 32);
    static const Type U64  = Type(UInt, 64);
    static const Type I8   = Type(Int, 8);
    static const Type I16  = Type(Int, 16);
    static const Type I32  = Type(Int, 32);
    static const Type I64  = Type(Int, 64);
    static const Type F8   = Type(Float, 8);
    static const Type F16  = Type(Float, 16);
    static const Type F32  = Type(Float, 32);
    static const Type F64  = Type(Float, 64);
};

class Expr {
public:
    Type type;
    int vector_width; // TODO(jrk) store vector width here or in type?
    typedef shared_ptr<Expr> Ptr;
    virtual ~Expr() {
    }
};

class SimpleStmt : public Stmt {
protected:
    Expr::Ptr expr;
};

class Block : public Stmt {
protected:
    vector<Stmt::Ptr> children;
};

class MapStmt : public Stmt {
protected:
    // TODO(jrk) how is domain defined?
    Stmt::Ptr body;
};

class IfStmt : public Stmt {
protected:
    LogicalExpr::Ptr condition;
    Stmt::Ptr trueBlock;
    Stmt::Ptr falseBlock;
};

class LoopStmt : public Stmt {
protected:
    // TODO(jrk) is iteration explicit here, or implicit within the block body?
    // TODO(jrk) break/continue?
    Stmt::Ptr body;
};

class BinOp : Expr {
public:
    typedef enum { Add, Sub, Mul, Div } Op;

protected:
    Op op;
    Expr::Ptr lhs, rhs;
};

class LogicalOp : public BinOp {
public:
    typedef shared_ptr<LogicalExpr> Ptr;
    typedef enum { And, Or, Not } Op;
    Type type = Type::Bool;

protected:
    Op op;
};

class CmpOp : public LogicalOp {
public:
    typedef enum { LT, LTE, GT, GTE, EQ, NEQ } Op;

protected:
    Op op;
};
#endif //_LLIR_H
