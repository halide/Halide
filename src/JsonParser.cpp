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
        internal_assert(val != members.end()) << "Key not found: " << key << "\n";
        return val->second;
    }
    bool key_exists(const std::string &key) const {
        return (members.find(key) != members.end());
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

    inline int get_bits(const std::string &s) {
        int num;
        std::stringstream v(s);
        v >> num;
        return num;
    }

    Type parse_type(const JSONNode &j) {
        auto *i = j.as<JSONString>();
        internal_assert(i);
        internal_assert(i->str.find("x") == std::string::npos);
        // TODO: more robust parsing
        if (starts_with(i->str, "uint")) {
            int bits = get_bits(i->str.substr(4, 2));
            std::cout << "str: " << i->str << " " << i->str.substr(4, 2)<< " bits: " << bits << "\n";
            return UInt(bits);
        } else if (starts_with(i->str, "int")) {
            int bits = get_bits(i->str.substr(3, 2));
            std::cout << "str" << i->str << " bits: " << bits << "\n";
            return Int(bits);
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
    Expr parse_StringImm(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return StringImm::make(parse_string(i->val_for("value")));
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

    std::vector<int> parse_array_int(const JSONNode &j) {
        std::vector<int> ints;
        auto *i = j.as<JSONArray>();
        internal_assert(i);
        for (auto &e : i->elements) {
            ints.push_back(parse_number(e));
        }
        return ints;
    }

    std::vector<Type> parse_array_type(const JSONNode &j) {
        std::vector<Type> types;
        auto *i = j.as<JSONArray>();
        internal_assert(i);
        for (auto &e : i->elements) {
            types.push_back(parse_type(e));
        }
        return types;
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

    ForType parse_ForType(const JSONNode &j) {
        std::map<std::string, ForType> ft_str =
            {{"Serial", ForType::Serial},
             {"Parallel", ForType::Parallel},
             {"Vectorized", ForType::Vectorized},
             {"Unrolled", ForType::Unrolled},
             {"Extern", ForType::Extern},
             {"GPUBlock", ForType::GPUBlock},
             {"GPUThread", ForType::GPUThread},
             {"GPULane", ForType::GPULane}};
        auto str = parse_string(j);
        return ft_str[str];
    }

    DeviceAPI parse_DeviceAPI(const JSONNode &j) {
        std::map<std::string, DeviceAPI> da_str =
            {{"None", DeviceAPI::None},
             {"Host", DeviceAPI::Host},
             {"Default_GPU", DeviceAPI::Default_GPU},
             {"CUDA", DeviceAPI::CUDA},
             {"OpenCL", DeviceAPI::OpenCL},
             {"GLSL", DeviceAPI::GLSL},
             {"OpenGLCompute", DeviceAPI::OpenGLCompute},
             {"Metal", DeviceAPI::Host},
             {"Hexagon", DeviceAPI::Hexagon},
             {"HexagonDma", DeviceAPI::HexagonDma},
             {"D3D12Compute", DeviceAPI::D3D12Compute}};
        auto str = parse_string(j);
        return da_str[str];
    }

    Stmt parse_For(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return For::make(parse_string(i->val_for("name")),
                         parse_Expr(i->val_for("min")),
                         parse_Expr(i->val_for("extent")),
                         parse_ForType(i->val_for("for_type")),
                         parse_DeviceAPI(i->val_for("device_api")),
                         parse_Stmt(i->val_for("body")));

    }

    Stmt parse_Acquire(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Acquire::make(parse_Expr(i->val_for("semaphore")),
                             parse_Expr(i->val_for("count")),
                             parse_Stmt(i->val_for("body")));
    }

    Expr parse_Shuffle(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Shuffle::make(parse_array_Expr(i->val_for("vectors")),
                             parse_array_int(i->val_for("indices")));
    }

    // A Range is a struct that is NOT an Expr, but contains
    // Exprs.
    Range parse_Range(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Range(parse_Expr(i->val_for("min")),
                     parse_Expr(i->val_for("extent")));
    }

    // A region is a vector of Ranges
    Region parse_Region(const JSONNode &j) {
        std::vector<Range> region;
        auto *i = j.as<JSONArray>();
        internal_assert(i);
        for (auto &e : i->elements) {
            region.push_back(parse_Range(e));
        }
        return region;
    }

    PrefetchDirective parse_PrefetchDirective(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        std::map<std::string, Halide::PrefetchBoundStrategy> strategy_str =
            {{"Clamp", Halide::PrefetchBoundStrategy::Clamp},
             {"GuardWithIf", Halide::PrefetchBoundStrategy::GuardWithIf},
             {"NonFaulting", Halide::PrefetchBoundStrategy::NonFaulting}};
        PrefetchDirective pd;
        pd.name = parse_string(i->val_for("name"));
        pd.var = parse_string(i->val_for("var"));
        pd.offset = parse_Expr(i->val_for("offset"));
        pd.strategy = strategy_str[parse_string(i->val_for("strategy"))];
        pd.param = parse_Parameter(i->val_for("param"));

        return pd;
    }

    Stmt parse_Prefetch(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Prefetch::make(parse_string(i->val_for("name")),
                              parse_array_type(i->val_for("types")),
                              parse_Region(i->val_for("bounds")),
                              parse_PrefetchDirective(i->val_for("prefetch")),
                              parse_Expr(i->val_for("condition")),
                              parse_Stmt(i->val_for("body")));
    }

    Stmt parse_Atomic(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Atomic::make(parse_string(i->val_for("producer_name")),
                            parse_string(i->val_for("mutex_name")),
                            parse_Stmt(i->val_for("body")));
    }

    Expr parse_VectorReduce(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        // This uses an internal enum to decide which operator
        VectorReduce::Operator ops[] = {VectorReduce::Operator::Add,
                                        VectorReduce::Operator::Mul,
                                        VectorReduce::Operator::Min,
                                        VectorReduce::Operator::Max,
                                        VectorReduce::Operator::And,
                                        VectorReduce::Operator::Or};
        return VectorReduce::make(ops[parse_number(i->val_for("op"))],
                                  parse_Expr(i->val_for("value")),
                                  parse_number(i->val_for("lanes")));
    }

    Stmt parse_Realize(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Realize::make(parse_string(i->val_for("name")),
                             parse_array_type(i->val_for("types")),
                             parse_MemoryType(i->val_for("memory_type")),
                             parse_Region(i->val_for("bounds")),
                             parse_Expr(i->val_for("condition")),
                             parse_Stmt(i->val_for("body")));
    }

    Expr parse_Load(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return Load::make(parse_type(i->val_for("type")),
                          parse_string(i->val_for("name")),
                          parse_Expr(i->val_for("index")),
                          //parse_Buffer(i->val_for("image")),
                          Buffer<>(),
                          parse_Parameter(i->val_for("param")),
                          parse_Expr(i->val_for("predicate")),
                          parse_ModulusRemainder(i->val_for("alignment")));
    }

    Call::CallType parse_CallType(const JSONNode &j) {
        std::map<std::string, Call::CallType> ct_map =
            {{"Image", Call::CallType::Image},
             {"Extern", Call::CallType::Extern},
             {"ExternCPlusPlus", Call::CallType::ExternCPlusPlus},
             {"PureExtern", Call::CallType::PureExtern},
             {"Halide", Call::CallType::Halide},
             {"Intrinsic", Call::CallType::Intrinsic},
             {"PureIntrinsic", Call::CallType::PureIntrinsic}};
        auto str = parse_string(j);
        return ct_map[str];

    }

    Expr parse_Call(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        Parameter p;
        if (i->key_exists("param")) {
            p = parse_Parameter(i->val_for("param"));
        }
        // Currently, intrinsics will get mapped to their names in the
        // constructor anyway, so this should be safe
        return Call::make(parse_type(i->val_for("type")),
                          parse_string(i->val_for("name")),
                          parse_array_Expr(i->val_for("args")),
                          parse_CallType(i->val_for("call_type")),
                          //parse_FunctionPtr(i->val_for("func")),
                          FunctionPtr(),
                          //parse_number(i->val_for("index")),
                          0,
                          //parse_Buffer(i->val_for("image")),
                          Buffer<>(),
                          p);
    }

    Expr parse_Variable(const JSONNode &j) {
        // TODO: image, reduction_domain
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        auto param = Parameter();
        if (i->key_exists("param")) {
            param = parse_Parameter(i->val_for("param"));
        }
        return Variable::make(parse_type(i->val_for("type")),
                              parse_string(i->val_for("name")),
                              param);
    }


    Stmt parse_Stmt(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        auto tp = i->val_for("_node_type").as<JSONString>()->str;
        if (starts_with(tp, "LetStmt")) {
            return parse_LetStmt(j);
        } else if (starts_with(tp, "AssertStmt")) {
            return parse_AssertStmt(j);
        } else if (starts_with(tp, "ProducerConsumer")) {
            return parse_ProducerConsumer(j);
        } else if (starts_with(tp, "For")) {
            return parse_For(j);
        } else if (starts_with(tp, "Acquire")) {
            return parse_Acquire(j);
        } else if (starts_with(tp, "Store")) {
            return parse_Store(j);
        } else if (starts_with(tp, "Provide")) {
            return parse_Provide(j);
        } else if (starts_with(tp, "Allocate")) {
            return parse_Allocate(j);
        } else if (starts_with(tp, "Free")) {
            return parse_Free(j);
        } else if (starts_with(tp, "Realize")) {
            return parse_Realize(j);
        } else if (starts_with(tp, "Block")) {
            return parse_Block(j);
        } else if (starts_with(tp, "Fork")) {
            return parse_Fork(j);
        } else if (starts_with(tp, "IfThenElse")) {
            return parse_IfThenElse(j);
        } else if (starts_with(tp, "Evaluate")) {
            return parse_Evaluate(j);
        } else if (starts_with(tp, "Prefetch")) {
            return parse_Prefetch(j);
        } else if (starts_with(tp, "Atomic")) {
            return parse_Atomic(j);
        } else if (starts_with(tp, "Stmt")) {
            return Stmt();
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
        } else if (starts_with(tp, "StringImm")) {
            return parse_StringImm(j);
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
        } else if (starts_with(tp, "Load")) {
            return parse_Load(j);
        } else if (starts_with(tp, "Ramp")) {
            return parse_Ramp(j);
        } else if (starts_with(tp, "Call")) {
            return parse_Call(j);
        } else if (starts_with(tp, "Let")) {
            return parse_Let(j);
        } else if (starts_with(tp, "Shuffle")) {
            return parse_Shuffle(j);
        } else if (starts_with(tp, "VectorReduce")) {
            return parse_VectorReduce(j);
        } else if (starts_with(tp, "Variable")) {
            return parse_Variable(j);
        } else if (starts_with(tp, "Expr")) {
            return Expr();
        } 
        internal_assert(false) << "No dispatch for " << tp << "\n";
        return Expr();
    }

    Argument::Kind parse_ArgumentKind(const JSONNode &j) {
        std::map<std::string, Argument::Kind> kind_str =
            {{"InputScalar", Argument::Kind::InputScalar},
             {"InputBuffer", Argument::Kind::InputBuffer},
             {"OutputBuffer", Argument::Kind::OutputBuffer}};
        auto str = parse_string(j);
        return kind_str[str];
    }

    ArgumentEstimates parse_ArgumentEstimates(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        ArgumentEstimates estimates;
        estimates.scalar_def = parse_Expr(i->val_for("scalar_def"));
        estimates.scalar_min = parse_Expr(i->val_for("scalar_min"));
        estimates.scalar_max = parse_Expr(i->val_for("scalar_max"));
        estimates.scalar_estimate = parse_Expr(i->val_for("scalar_estimate"));
        estimates.buffer_estimates = parse_Region(i->val_for("buffer_estimates"));
        return estimates;
    }

    std::vector<LoweredArgument> parse_array_LoweredArgument(const JSONNode &j) {
        auto *i = j.as<JSONArray>();
        internal_assert(i);
        std::vector<LoweredArgument> ret;
        for (auto &e: i->elements) {
            auto *arg_obj = e.as<JSONObject>();
            internal_assert(arg_obj);
            LoweredArgument argument(parse_string(arg_obj->val_for("name")),
                                     parse_ArgumentKind(arg_obj->val_for("kind")),
                                     parse_type(arg_obj->val_for("type")),
                                     parse_number(arg_obj->val_for("dimensions")),
                                     parse_ArgumentEstimates(arg_obj->val_for("argument_estimates")));
            argument.alignment = parse_ModulusRemainder(arg_obj->val_for("alignment"));
            ret.push_back(argument);
        }
        return ret;
    }

    LinkageType parse_LinkageType(const JSONNode &j) {
        std::map<std::string, LinkageType> linkage_str =
            {{"External", LinkageType::External},
             {"ExternalPlusMetadata", LinkageType::ExternalPlusMetadata},
             {"Internal", LinkageType::Internal}};
        auto str = parse_string(j);
        return linkage_str[str];
    }

    NameMangling parse_NameMangling(const JSONNode &j) {
        auto *i = j.as<JSONString>();
        internal_assert(i);
        return NameMangling::Default;
    }

    LoweredFunc parse_LoweredFunc(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        return LoweredFunc(parse_string(i->val_for("name")),
                           parse_array_LoweredArgument(i->val_for("args")),
                           parse_Stmt(i->val_for("body")),
                           parse_LinkageType(i->val_for("linkage")),
                           parse_NameMangling(i->val_for("name_mangling")));
    }

    Module parse_Module(const JSONNode &j) {
        auto *i = j.as<JSONObject>();
        internal_assert(i);
        auto name = parse_string(i->val_for("name"));
        Target target(parse_string(i->val_for("target")));

        Module m(name, target);

        // Iterate through the funcs and add them
        auto *funcs = i->val_for("functions").as<JSONArray>();
        internal_assert(funcs);
        for (auto &e : funcs->elements) {
            m.append(parse_LoweredFunc(e));
        }

        return m;
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
        Internal::JSONParser p(read_entire_file(fname));
        //Internal::JSONParser p("[10, -2203, 5.08, 9.9.9]");
        //Internal::JSONParser p("{ \"_node_type\" : \"IntImm\", \"type\" : \"int64_t\", \"value\" : 1873 }");

        auto jsn = p.parse();
        std::cout << "==============\n";
        Internal::HalideJSONParser hp;
        //std::cout << hp.parse_IntImm(jsn);
        hp.parse_Module(jsn);


        return m;
    }


  //} // namespace Internal
} // namespace Halide
