#include "IROperator.h"
#include "IRVisitor.h"
#include "IRPrinter.h"
#include "Scope.h"
#include "IntrusivePtr.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdio.h>

namespace Halide {
  //namespace Internal {

namespace Internal {

enum JSONNodeType {
    Object,
    Array,
    String,
    Number,
    TrueValue,
    FalseValue,
    NullValue
};

struct JSONIRNode {
    JSONIRNode(JSONNodeType t) : node_type(t) { }
    virtual ~JSONIRNode() = default;
    JSONNodeType node_type;
    mutable RefCount ref_count;
};

template<>
inline RefCount &ref_count<JSONIRNode>(const JSONIRNode *t) noexcept {
    return t->ref_count;
}

template<>
inline void destroy<JSONIRNode>(const JSONIRNode *t) {
    delete t;
}

template<typename T>
struct JSONBaseNode : public JSONIRNode {
    JSONBaseNode() : JSONIRNode(T::_json_node_type) { }
    ~JSONBaseNode() override = default;
};

struct JSONIRHandle : public IntrusivePtr<const JSONIRNode> {
    JSONIRHandle() = default;
    JSONIRHandle(const JSONIRNode *p) : IntrusivePtr<const JSONIRNode>(p) { }

    template<typename T>
    const T *as() const {
        if (ptr && ptr->node_type == T::_json_node_type) {
            return (const T*)ptr;
        } else {
            return nullptr;
        }
    }
    JSONNodeType node_type() const {
        return ptr->node_type;
    }
};


struct JSONNode : public JSONIRHandle {
    JSONNode() = default;
    template<typename T>
    JSONNode(const JSONBaseNode<T> *n) : JSONIRHandle(n) { }
    template<typename T>
    const JSONBaseNode<T> *get() const {
        return (const JSONBaseNode<T>*)ptr;
    }
};

struct JSONObject : public JSONBaseNode<JSONObject> {
    std::map<std::string, JSONNode> members;
    static const JSONNodeType _json_node_type = Object;
    static JSONNode make(std::map<std::string, JSONNode> &members) {
        JSONObject *node = new JSONObject;
        node->members = std::move(members);
        return node;
    }
    const JSONNode val_for(const std::string &key) const {
        auto val = members.find(key);
        internal_assert(val != members.end());
        return val->second;
    }
};

struct JSONArray : public JSONBaseNode<JSONArray> {
    std::vector<JSONNode> elements;
    static const JSONNodeType _json_node_type = Array;
    static JSONNode make(std::vector<JSONNode> &elements) {
        JSONArray *node = new JSONArray;
        node->elements = std::move(elements);
        return node;
    }
};

struct JSONString : public JSONBaseNode<JSONString> {
    std::string str;
    static const JSONNodeType _json_node_type = String;
    static JSONNode make(std::string &str) {
        JSONString *node = new JSONString;
        node->str = std::move(str);
        return node;
    }
};

struct JSONNumber : public JSONBaseNode<JSONNumber> {
    uint64_t value;
    bool is_float;
    static const JSONNodeType _json_node_type = Number;
    static JSONNode make(uint64_t value, bool is_float) {
        JSONNumber *node = new JSONNumber;
        node->value = value;
        node->is_float = is_float;
        return node;
    }
};

struct JSONTrueValue : public JSONBaseNode<JSONTrueValue> {
    static const JSONNodeType _json_node_type = TrueValue;
    static JSONNode make() {
        JSONTrueValue *node = new JSONTrueValue;
        return node;
    }
};

struct JSONFalseValue : public JSONBaseNode<JSONFalseValue> {
    static const JSONNodeType _json_node_type = FalseValue;
    static JSONNode make() {
        JSONFalseValue *node = new JSONFalseValue;
        return node;
    }
};

struct JSONNullValue : public JSONBaseNode<JSONNullValue> {
    static const JSONNodeType _json_node_type = NullValue;
    static JSONNode make() {
        JSONNullValue *node = new JSONNullValue;
        return node;
    }
};

struct JSONParser {
    std::string str;
    size_t loc;

    JSONParser(const std::string &str) : str(str), loc(0) { }

    std::string parse_raw_string() {
        std::cout << "parsing raw string\n";
        internal_assert(str[loc] == '"');
        size_t start = ++loc; // consume the starting quote
        while (loc < str.size()) {
            if (str[loc] == '"') {
                std::cout << "   raw string returns: " << str.substr(start, loc-start) << "\n";
                return str.substr(start, (loc++-start));
            }
            loc++;
        }
        internal_assert(false);
        return std::string();
    }

    JSONNode parse_string() {
        std::cout << "parsing string\n";
        internal_assert(str[loc] == '"');
        auto str = parse_raw_string();
        return JSONString::make(str);
    }

    std::pair<std::string, JSONNode> parse_key_val() {
        std::cout << "parsing key_val\n";
        auto key = parse_raw_string();
        while (loc < str.size()) {
            switch(str[loc]) {
            case ' ':
            case '\n':
            case '\t':
                loc++;
                break;
            case ':':
                // consume :
                loc++;
                return std::make_pair(key, parse());
            default:
                internal_assert(false == str[loc]);
                break;
            }
        }
        return std::make_pair(key, JSONNullValue::make());
    }

    JSONNode parse_number() {
        // TODO: EE/ee/e/E
        std::cout << "parsing number\n";
        auto zero_allowed = true;
        auto minus_allowed = true;
        auto dot_allowed = false;
        auto in_fraction = false;
        auto is_float = false;
        size_t start = loc;
        size_t end = loc+1;

        while (loc < str.size()) {
            switch (str[loc]) {
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                zero_allowed = true;
                minus_allowed = false;
                dot_allowed = !in_fraction;
                loc++;
                end++;
                break;
            case '-':
                internal_assert(minus_allowed);
                minus_allowed = false;
                zero_allowed = false;
                loc++;
                end++;
                break;
            case '0':
                internal_assert(zero_allowed);
                minus_allowed = false;
                dot_allowed = !in_fraction;
                loc++;
                end++;
                break;
            case '.':
                internal_assert(dot_allowed && !in_fraction);
                is_float = true;
                in_fraction = true;
                dot_allowed = false;
                zero_allowed = true;
                loc++;
                end++;
                break;
            default:
                std::stringstream v(str.substr(start, end-start));
                uint64_t value;
                double f_value;
                if (is_float) {
                    v >> f_value;
                    return JSONNumber::make(f_value, is_float);
                } else {
                    v >> value;
                    return JSONNumber::make(value, is_float);
                }
            }
        }
        return JSONNullValue::make();
    }

    JSONNode parse_true() {
        std::cout << "parsing true\n";
        internal_assert(str.size() >= loc+4);
        internal_assert(str.substr(loc, 4) == "true");
        loc += 4;
        return JSONTrueValue::make();
    }
    JSONNode parse_false() {
        std::cout << "parsing false\n";
        internal_assert(str.size() >= loc+5);
        internal_assert(str.substr(loc, 5) == "false");
        loc += 5;
        return JSONFalseValue::make();
    }
    JSONNode parse_null() {
        std::cout << "parsing null\n";
        internal_assert(str.size() >= loc+4);
        internal_assert(str.substr(loc, 4) == "null");
        loc += 4;
        return JSONNullValue::make();
    }

    JSONNode parse_object() {
        std::cout << "parsing object\n";
        std::map<std::string, JSONNode> members;

        internal_assert(str[loc] == '{');
        loc++;

        bool comma_allowed = false;

        while (loc < str.size()) {
            std::cout << "str[loc]:" << str[loc] << "\n";
            switch (str[loc]) {
            case ' ':
            case '\n':
            case '\t':
                loc++;
                break;
            case '"':
                // go off and parse a key-val pair
                //key_val = parse_key_val();
                //object.members[key_val.first] = key_val.second;
                members.insert(parse_key_val());
                comma_allowed = true;
                break;
            case ',':
                // separator
                loc++;
                comma_allowed = false;
                break;
            case '}':
                loc++;  // consume }
                return JSONObject::make(members);
            default:
                internal_assert(false) << "error at loc " << loc << ": " << str.substr(loc, 5) << "\n";
            }

        }
        return JSONNullValue::make();
    }

    JSONNode parse_array() {
        std::cout << "parsing array\n";
        internal_assert(str[loc] == '[');
        loc++;
        std::vector<JSONNode> elements;


        while (loc < str.size()) {
            switch(str[loc]) {
            case ' ':
            case '\n':
            case '\t':
                loc++;
                break;
            case ',':
                // separator of objects
                loc++;
                break;
            case ']':
                loc++;
                return JSONArray::make(elements);
            default:
                // try to parse the next value
                elements.push_back(parse());
            }
        }

        return JSONNullValue::make();
    }

    JSONNode parse() {
        while (loc < str.size()) {
            switch (str[loc]) {
            case ' ':
            case '\n':
            case '\t':
                loc++;
                continue;
            case '{':
                return parse_object();
            case '[':
                return parse_array();
            case '"':
                return parse_string();
            case 't':
                return parse_true();
            case 'f':
                return parse_false();
            case 'n':
                return parse_null();
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case '-':
                return parse_number();
            default:
                internal_assert(false);
                break;
            }
        }
        return JSONNullValue::make();
    }


};

struct HalideJSONParser {
    // Take a parsed JSON file and generate something out of it
    //JSONNode json;

    //HalideJSONParser(JSONNode &json) : json(json) { }

    uint64_t parse_number(const JSONNode &j) {
        auto *i = j.as<JSONNumber>();
        internal_assert(i);
        return i->value;
    }

    const std::string &parse_string(const JSONNode &j) {
        auto *i = j.as<JSONString>();
        internal_assert(i);
        return i->str;
    }

    bool parse_bool(const JSONNode &j) {
        auto *t = j.as<JSONTrueValue>();
        if (t) {
            return true;
        }
        auto *f = j.as<JSONFalseValue>();
        if (f) {
            return false;
        }
        internal_assert(false);
        return false;
    }

    Type parse_type(const JSONNode &j) {
        auto *i = j.as<JSONString>();
        internal_assert(i);
        internal_assert(i->str.find("x") == std::string::npos);
        // TODO: more robust parsing
        if (starts_with(i->str, "uint")) {
            return UInt(64);
        } else if (starts_with(i->str, "int")) {
            return Int(64);
        } else if (starts_with(i->str, "float")) {
                return Float(64);
        } else {
            return Int(64);
        }
    }

    template<typename T>
    Expr parse_immediate(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        auto num = parse_number(i->val_for("value"));
        auto t = parse_type(i->val_for("type"));
        return T::make(t, num);
    }

    Expr parse_IntImm(const JSONNode &j) {
        return parse_immediate<IntImm>(j);
    }
    Expr parse_UIntImm(const JSONNode &j) {
        return parse_immediate<UIntImm>(j);
    }
    Expr parse_FloatImm(const JSONNode &j) {
        return parse_immediate<FloatImm>(j);
    }

    Expr parse_Cast(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Cast::make(parse_type(i->val_for("type")),
                          parse_Expr(i->val_for("value")));
    }

    template<typename T>
    Expr parse_binop(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return T::make(parse_Expr(i->val_for("a")),
                       parse_Expr(i->val_for("b")));
    }

    Expr parse_Add(const JSONNode &j) {
        return parse_binop<Add>(j);
    }
    Expr parse_Sub(const JSONNode &j) {
        return parse_binop<Sub>(j);
    }
    Expr parse_Mul(const JSONNode &j) {
        return parse_binop<Mul>(j);
    }
    Expr parse_Div(const JSONNode &j) {
        return parse_binop<Div>(j);
    }
    Expr parse_Mod(const JSONNode &j) {
        return parse_binop<Mod>(j);
    }
    Expr parse_Max(const JSONNode &j) {
        return parse_binop<Max>(j);
    }
    Expr parse_Min(const JSONNode &j) {
        return parse_binop<Min>(j);
    }
    Expr parse_EQ(const JSONNode &j) {
        return parse_binop<EQ>(j);
    }
    Expr parse_NE(const JSONNode &j) {
        return parse_binop<NE>(j);
    }
    Expr parse_LT(const JSONNode &j) {
        return parse_binop<LT>(j);
    }
    Expr parse_LE(const JSONNode &j) {
        return parse_binop<LE>(j);
    }
    Expr parse_GT(const JSONNode &j) {
        return parse_binop<GT>(j);
    }
    Expr parse_GE(const JSONNode &j) {
        return parse_binop<GE>(j);
    }
    Expr parse_And(const JSONNode &j) {
        return parse_binop<And>(j);
    }
    Expr parse_Or(const JSONNode &j) {
        return parse_binop<Or>(j);
    }

    Expr parse_Not(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Not::make(parse_Expr(i->val_for("a")));
    }

    Expr parse_Select(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Select::make(parse_Expr(i->val_for("condition")),
                            parse_Expr(i->val_for("true_value")),
                            parse_Expr(i->val_for("false_value")));
    }

    Expr parse_Ramp(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Ramp::make(parse_Expr(i->val_for("base")),
                          parse_Expr(i->val_for("stride")),
                          parse_number(i->val_for("lanes")));
    }

    Expr parse_Broadcast(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Broadcast::make(parse_Expr(i->val_for("value")),
                               parse_number(i->val_for("lanes")));
    }

    Expr parse_Let(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Let::make(parse_string(i->val_for("name")),
                         parse_Expr(i->val_for("value")),
                         parse_Expr(i->val_for("body")));
    }

    Stmt parse_LetStmt(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return LetStmt::make(parse_string(i->val_for("name")),
                             parse_Expr(i->val_for("value")),
                             parse_Stmt(i->val_for("body")));
    }

    Stmt parse_AssertStmt(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return AssertStmt::make(parse_Expr(i->val_for("condition")),
                                parse_Expr(i->val_for("message")));
    }

    Stmt parse_ProducerConsumer(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return ProducerConsumer::make(parse_string(i->val_for("name")),
                                      parse_bool(i->val_for("is_producer")),
                                      parse_Stmt(i->val_for("body")));
    }

    ModulusRemainder parse_ModulusRemainder(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return ModulusRemainder(parse_number(i->val_for("modulus")),
                                parse_number(i->val_for("remainder")));
    }

    Parameter parse_Parameter(const JSONNode &j) {
        auto *i =j.as<JSONObject>();
        internal_assert(i);
        return Parameter(parse_type(i->val_for("type")),
                         parse_bool(i->val_for("is_buffer")),
                         parse_number(i->val_for("dimensions")),
                         parse_string(i->val_for("name")));
    }


    Stmt parse_Store(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Store::make(parse_string(i->val_for("name")),
                           parse_Expr(i->val_for("value")),
                           parse_Expr(i->val_for("index")),
                           parse_Parameter(i->val_for("param")),
                           parse_Expr(i->val_for("predicate")),
                           parse_ModulusRemainder(i->val_for("alignment")));
    }

    Stmt parse_Provide(const JSONNode &j) {
        internal_assert(false) << "Provide should not appear in JSON input\n";
        return Stmt();
    }

    MemoryType parse_MemoryType(const JSONNode &j) {
        auto str = parse_string(j);
        if (str == "Auto") {
            return MemoryType::Auto;
        } else if (str == "Heap") {
            return MemoryType::Heap;
        } else if (str == "Stack") {
            return MemoryType::Stack;
        } else if (str == "Register") {
            return MemoryType::Register;
        } else if (str == "GPUShared") {
            return MemoryType::GPUShared;
        } else if (str == "LockedCache") {
            return MemoryType::LockedCache;
        } else if (str == "VTCM") {
            return MemoryType::VTCM;
        }
        internal_assert(false);
        return MemoryType::Auto;
    }

    std::vector<Expr> parse_array_Expr(const JSONNode &j) {
        std::vector<Expr> exprs;
        auto *i = j.as<JSONArray>();
        internal_assert(i);
        for (auto &e : i->elements) {
            exprs.push_back(parse_Expr(e));
        }
        return exprs;
    }

    Stmt parse_Allocate(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Allocate::make(parse_string(i->val_for("name")),
                              parse_type(i->val_for("type")),
                              parse_MemoryType(i->val_for("memory_type")),
                              parse_array_Expr(i->val_for("extents")),
                              parse_Expr(i->val_for("condition")),
                              parse_Stmt(i->val_for("body")),
                              parse_Expr(i->val_for("new_expr")),
                              parse_string(i->val_for("free_function")));
    }

    Stmt parse_Free(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Free::make(parse_string(i->val_for("name")));
    }

    Stmt parse_Block(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Block::make(parse_Stmt(i->val_for("first")),
                           parse_Stmt(i->val_for("rest")));
    }

    Stmt parse_Fork(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Fork::make(parse_Stmt(i->val_for("first")),
                          parse_Stmt(i->val_for("rest")));
    }

    Stmt parse_IfThenElse(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return IfThenElse::make(parse_Expr(i->val_for("condition")),
                                parse_Stmt(i->val_for("then_case")),
                                parse_Stmt(i->val_for("else_case")));
    }

    Stmt parse_Evaluate(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Evaluate::make(parse_Expr(i->val_for("value")));
    }

    Stmt parse_Stmt(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        auto tp = i->val_for("_node_type").as<JSONString>()->str;
        if (starts_with(tp, "LetStmt")) {
            return parse_LetStmt(j);
        } else if (starts_with(tp, "AssertStmt")) {
            return parse_AssertStmt(j);
        } else if (starts_with(tp, "Store")) {
            return parse_Store(j);
        }
        internal_assert(false);
        return Stmt();
    }


    Expr parse_Expr(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        auto tp = i->val_for("_node_type").as<JSONString>()->str;
        if (starts_with(tp, "IntImm")) {
            return parse_IntImm(j);
        } else if (starts_with(tp, "UIntImm")) {
            return parse_UIntImm(j);
        } else if (starts_with(tp, "FloatImm")) {
            return parse_FloatImm(j);
        } else if (starts_with(tp, "Add")) {
            return parse_Add(j);
        } else if (starts_with(tp, "Sub")) {
            return parse_Sub(j);
        } else if (starts_with(tp, "Mul")) {
            return parse_Mul(j);
        } else if (starts_with(tp, "Div")) {
            return parse_Div(j);
        } else if (starts_with(tp, "Mod")) {
            return parse_Mod(j);
        } else if (starts_with(tp, "Max")) {
            return parse_Max(j);
        } else if (starts_with(tp, "Min")) {
            return parse_Min(j);
        } else if (starts_with(tp, "EQ")) {
            return parse_EQ(j);
        } else if (starts_with(tp, "NE")) {
            return parse_NE(j);
        } else if (starts_with(tp, "LT")) {
            return parse_LT(j);
        } else if (starts_with(tp, "LE")) {
            return parse_LE(j);
        } else if (starts_with(tp, "GT")) {
            return parse_GT(j);
        } else if (starts_with(tp, "GE")) {
            return parse_GE(j);
        } else if (starts_with(tp, "And")) {
            return parse_And(j);
        } else if (starts_with(tp, "Or")) {
            return parse_Or(j);
        } else if (starts_with(tp, "Not")) {
            return parse_Not(j);
        } else if (starts_with(tp, "Cast")) {
            return parse_Cast(j);
        } else if (starts_with(tp, "Broadcast")) {
            return parse_Broadcast(j);
        } else if (starts_with(tp, "Ramp")) {
            return parse_Ramp(j);
        } else if (starts_with(tp, "Select")) {
            return parse_Select(j);
        }
        internal_assert(false) << "No dispatch for " << tp << "\n";
        return Expr();
    }

};


}

    namespace {
        std::string read_entire_file(const std::string &fname) {
            std::string str;
            std::ifstream instream(fname.c_str());
            std::getline(instream, str, std::string::traits_type::to_char_type(
                             std::string::traits_type::eof()));
            return str;
        }
    }

    Module parse_from_json_file(const std::string &fname) {
        Module m("parsed", Target());
        //Internal::JSONParser p("\n{ \"a\" : \"b\" , \"c\" : \"d\"}");
        //Internal::JSONParser p("\n[ \"a\" , \"b\" ]");
        //Internal::JSONParser p(read_entire_file(fname));
        //Internal::JSONParser p("[10, -2203, 5.08, 9.9.9]");
        Internal::JSONParser p("{ \"_node_type\" : \"IntImm\", \"type\" : \"int64_t\", \"value\" : 1873 }");
        auto jsn = p.parse();
        std::cout << "==============\n";
        Internal::HalideJSONParser hp;
        std::cout << hp.parse_IntImm(jsn);


        return m;
    }


  //} // namespace Internal
} // namespace Halide
