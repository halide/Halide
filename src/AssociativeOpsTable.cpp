#include "AssociativeOpsTable.h"
#include "IRPrinter.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;

namespace {

enum class RootExpr {
    Add = 0,
    Mul = 1,
    Max = 2,
    Min = 3,
    Sub = 4,
    Select = 5,
    And = 6,
    Or = 7,
    Cast = 8,
    Unknown = 9, // Not supported IR type
};

enum class ValType {
    UInt1 = 0,
    UInt8 = 1,
    UInt16 = 2,
    UInt32 = 3,
    UInt64 = 4,
    Int8 = 5,
    Int16 = 6,
    Int32 = 7,
    Int64 = 8,
    Float32 = 9,
    Float64 = 10,
    All = 11, // General type (including all previous types)
};

ValType convert_halide_type_to_val_type(const Type &halide_t) {
    internal_assert(halide_t.is_scalar() && !halide_t.is_handle());

    ValType val_t;
    if (halide_t.is_uint()) {
        if (halide_t.bits() == 1) { // Bool
            val_t = ValType::UInt1;
        } else if (halide_t.bits() == 8) {
            val_t = ValType::UInt8;
        } else if (halide_t.bits() == 16) {
            val_t = ValType::UInt16;
        } else if (halide_t.bits() == 32) {
            val_t = ValType::UInt32;
        } else {
            internal_assert(halide_t.bits() == 64);
            val_t = ValType::UInt64;
        }
    } else if (halide_t.is_int()) {
        if (halide_t.bits() == 8) {
            val_t = ValType::Int8;
        } else if (halide_t.bits() == 16) {
            val_t = ValType::Int16;
        } else if (halide_t.bits() == 32) {
            val_t = ValType::Int32;
        } else {
            internal_assert(halide_t.bits() == 64);
            val_t = ValType::Int64;
        }
    } else {
        internal_assert(halide_t.is_float());
        if (halide_t.bits() == 32) {
            val_t = ValType::Float32;
        } else {
            internal_assert(halide_t.bits() == 64);
            val_t = ValType::Float64;
        }
    }
    return val_t;
}

vector<ValType> convert_halide_types_to_val_types(const vector<Type> &halide_types) {
    vector<ValType> val_types(halide_types.size());
    for (size_t i = 0; i < halide_types.size(); ++i) {
        val_types[i] = convert_halide_type_to_val_type(halide_types[i]);
    }
    return val_types;
}

struct TableKey {
    vector<ValType> types;
    RootExpr root;
    size_t dim;
    TableKey(ValType t, RootExpr r, size_t d) : types({t}), root(r), dim(d) {}
    TableKey(const vector<ValType> &t, RootExpr r, size_t d) : types(t), root(r), dim(d) {}

    bool operator==(const TableKey &other) const {
        return (types == other.types) && (root == other.root) && (dim == other.dim);
    }
    bool operator<(const TableKey &other) const {
        if (types < other.types) {
            return true;
        } else if (types > other.types) {
            return false;
        }
        if (root < other.root) {
            return true;
        } else if (root > other.root) {
            return false;
        }
        return (dim < other.dim);
    }
};

static map<TableKey, vector<AssociativePattern>> pattern_tables;

#define declare_vars(t, index)                  \
    Expr x##index = Variable::make(t, "x" + std::to_string(index)); \
    Expr y##index = Variable::make(t, "y" + std::to_string(index)); \
    Expr k##index = Variable::make(t, "k" + std::to_string(index)); \
    Expr zero_##index = make_const(t, 0);       \
    Expr one_##index = make_const(t, 1);        \
    Expr neg_one_##index = make_const(t, -1);   \
    Expr tmax_##index = t.max();                \
    Expr tmin_##index = t.min();                \

#define declare_vars_single(types)          \
    internal_assert(types.size() == 1);     \
    declare_vars(types[0], 0)               \

#define declare_vars_double(types)          \
    internal_assert(types.size() == 2);     \
    declare_vars(types[0], 0)               \
    declare_vars(types[1], 1)               \

void populate_ops_table_single_general_add(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_single(types);
    table.push_back({x0 + y0, zero_0, true});
}

void populate_ops_table_single_general_mul(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_single(types);
    table.push_back({x0 * y0, one_0, true});
}

void populate_ops_table_single_general_max(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_single(types);
    table.push_back({max(x0, y0), tmin_0, true});
}

void populate_ops_table_single_general_min(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_single(types);
    table.push_back({min(x0, y0), tmax_0, true});
}

void populate_ops_table_single_general_sub(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_single(types);
}

void populate_ops_table_single_general_select(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_single(types);
}

void populate_ops_table_double_general_add(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_double(types);
    if (types[0] == types[1]) {
        table.push_back({{x0 + y0, x0 + y1}, {zero_0, zero_1}, true});
    }
}

void populate_ops_table_double_general_mul(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_double(types);
}

void populate_ops_table_double_general_max(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_double(types);
    table.push_back({{max(x0, y0), select(y0 < x0, x1, y1)}, {tmin_0, zero_1}, true});
}

void populate_ops_table_double_general_min(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_double(types);
    table.push_back({{min(x0, y0), select(x0 < y0, x1, y1)}, {tmax_0, zero_1}, true});
}

void populate_ops_table_double_general_sub(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_double(types);
    if (types[0] == types[1]) {
        table.push_back({{x0 * y0 - x1 * y1, x1 * y0 + x0 * y1}, {one_0, zero_1}, true});
        table.push_back({{x0 * y0 - y1 * x1, x1 * y0 + y1 * x0}, {one_0, zero_1}, true});
    }
}

void populate_ops_table_double_general_select(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_double(types);
}

void populate_ops_table_single_uint1_and(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_single(types);
    table.push_back({x0 && y0, one_0, true});
}

void populate_ops_table_single_uint1_or(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_single(types);
    table.push_back({x0 || y0, zero_0, true});
}

void populate_ops_table_single_uint8_cast(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_single(types);
    Expr k0_uint16 = Variable::make(UInt(16), "k0");
    Expr k0_uint32 = Variable::make(UInt(32), "k0");
    Expr k0_uint64 = Variable::make(UInt(64), "k0");
    table.push_back({cast<uint8_t>(min(cast<uint16_t>(x0 + y0), k0_uint16)), zero_0, true});
    table.push_back({cast<uint8_t>(min(cast<uint32_t>(x0 + y0), k0_uint32)), zero_0, true});
    table.push_back({cast<uint8_t>(min(cast<uint64_t>(x0 + y0), k0_uint64)), zero_0, true});
}

void populate_ops_table_single_uint8_select(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_single(types);
    table.push_back({select(x0 > tmax_0 - y0, tmax_0, y0), zero_0, true});  // Saturating add
    table.push_back({select(x0 < -y0, y0, tmax_0), zero_0, true});          // Saturating add
}

void populate_ops_table_single_uint16_cast(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_single(types);
    Expr k0_uint32 = Variable::make(UInt(32), "k0");
    Expr k0_uint64 = Variable::make(UInt(64), "k0");
    table.push_back({cast<uint16_t>(min(cast<uint32_t>(x0 + y0), k0_uint32)), zero_0, true});
    table.push_back({cast<uint16_t>(min(cast<uint64_t>(x0 + y0), k0_uint64)), zero_0, true});
}

void populate_ops_table_single_uint16_select(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_single(types);
    table.push_back({select(x0 > tmax_0 - y0, tmax_0, y0), zero_0, true});  // Saturating add
    table.push_back({select(x0 < -y0, y0, tmax_0), zero_0, true});          // Saturating add
}

void populate_ops_table_single_uint32_cast(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_single(types);
    Expr k0_uint64 = Variable::make(UInt(64), "k0");
    table.push_back({cast<uint32_t>(min(cast<uint64_t>(x0 + y0), k0_uint64)), zero_0, true});
}

void populate_ops_table_single_uint32_select(const vector<Type> &types, vector<AssociativePattern> &table) {
    declare_vars_single(types);
    table.push_back({select(x0 > tmax_0 - y0, tmax_0, y0), zero_0, true});  // Saturating add
    table.push_back({select(x0 < -y0, y0, tmax_0), zero_0, true});          // Saturating add
}

static const map<TableKey, void(*)(const vector<Type> &types, vector<AssociativePattern> &)> val_type_to_populate_luts_fn = {
    {TableKey(ValType::All, RootExpr::Add, 1), &populate_ops_table_single_general_add},
    {TableKey(ValType::All, RootExpr::Mul, 1), &populate_ops_table_single_general_mul},
    {TableKey(ValType::All, RootExpr::Max, 1), &populate_ops_table_single_general_max},
    {TableKey(ValType::All, RootExpr::Min, 1), &populate_ops_table_single_general_min},
    {TableKey(ValType::All, RootExpr::Sub, 1), &populate_ops_table_single_general_sub},
    {TableKey(ValType::All, RootExpr::Select, 1), &populate_ops_table_single_general_select},
    {TableKey(ValType::All, RootExpr::Add, 2), &populate_ops_table_double_general_add},
    {TableKey(ValType::All, RootExpr::Mul, 2), &populate_ops_table_double_general_mul},
    {TableKey(ValType::All, RootExpr::Max, 2), &populate_ops_table_double_general_max},
    {TableKey(ValType::All, RootExpr::Min, 2), &populate_ops_table_double_general_min},
    {TableKey(ValType::All, RootExpr::Sub, 2), &populate_ops_table_double_general_sub},
    {TableKey(ValType::All, RootExpr::Select, 2), &populate_ops_table_double_general_select},

    {TableKey(ValType::UInt1, RootExpr::And, 1), &populate_ops_table_single_uint1_and},
    {TableKey(ValType::UInt1, RootExpr::Or, 1), &populate_ops_table_single_uint1_or},

    {TableKey(ValType::UInt8, RootExpr::Cast, 1), &populate_ops_table_single_uint8_cast},
    {TableKey(ValType::UInt8, RootExpr::Select, 1), &populate_ops_table_single_uint8_select},

    {TableKey(ValType::UInt16, RootExpr::Cast, 1), &populate_ops_table_single_uint16_cast},
    {TableKey(ValType::UInt16, RootExpr::Select, 1), &populate_ops_table_single_uint16_select},

    {TableKey(ValType::UInt32, RootExpr::Cast, 1), &populate_ops_table_single_uint32_cast},
    {TableKey(ValType::UInt32, RootExpr::Select, 1), &populate_ops_table_single_uint32_select},
};

const vector<AssociativePattern> &get_ops_table_helper(const vector<Type> &types, RootExpr root, size_t dim) {
    TableKey gen_key(ValType::All, root, dim);
    TableKey key(convert_halide_types_to_val_types(types), root, dim);

    const auto &table_it = pattern_tables.find(key);
    if (table_it == pattern_tables.end()) { // Populate the table if we haven't done so previously
        vector<AssociativePattern> &table = pattern_tables[key];

        // Populate the general associative op LUT
        const auto &gen_iter = val_type_to_populate_luts_fn.find(gen_key);
        if (gen_iter != val_type_to_populate_luts_fn.end()) {
            gen_iter->second(types, table);
        }

        // Populate the type-specific associative op LUT
        const auto &iter = val_type_to_populate_luts_fn.find(key);
        if (iter != val_type_to_populate_luts_fn.end()) {
            iter->second(types, table);
        }

        return table;
    }
    return table_it->second;
}

std::string print_types(const vector<Type> &types) {
    std::ostringstream stream;
    stream << "{";
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            stream << ", ";
        }
        stream << types[i];
    }
    stream << "}";
    return stream.str();
}

}  // anonymous namespace

const vector<AssociativePattern> &get_ops_table(const vector<Expr> &exprs) {
    internal_assert(!exprs.empty());

    static vector<AssociativePattern> empty;

    if (exprs.size() > 2) {
        debug(5) << "Returning empty table since tuple size is larger than 2\n";
        return empty;
    }

    vector<Type> types(exprs.size());
    for (size_t i = 0; i < exprs.size(); ++i) {
        types[i] = exprs[i].type();
    }

    RootExpr root = RootExpr::Unknown;
    if (exprs[0].as<Halide::Internal::Add>()) {
        debug(5) << "Returning Add root table for type " << print_types(types) << "\n";
        root = RootExpr::Add;
    } else if (exprs[0].as<Halide::Internal::Sub>()) {
        debug(5) << "Returning Sub root table for type " << print_types(types) << "\n";
        root = RootExpr::Sub;
    } else if (exprs[0].as<Halide::Internal::Mul>()) {
        debug(5) << "Returning Mul root table for type " << print_types(types) << "\n";
        root = RootExpr::Mul;
    } else if (exprs[0].as<Halide::Internal::Min>()) {
        debug(5) << "Returning Min root table for type " << print_types(types) << "\n";
        root = RootExpr::Min;
    } else if (exprs[0].as<Halide::Internal::Max>()) {
        debug(5) << "Returning Max root table for type " << print_types(types) << "\n";
        root = RootExpr::Max;
    } else if (exprs[0].as<Halide::Internal::Select>()) {
        debug(5) << "Returning Select root table for type " << print_types(types) << "\n";
        root = RootExpr::Select;
    } else if (exprs[0].as<Halide::Internal::And>()) {
        debug(5) << "Returning And root table for type " << print_types(types) << "\n";
        root = RootExpr::And;
    } else if (exprs[0].as<Halide::Internal::Or>()) {
        debug(5) << "Returning Or root table for type " << print_types(types) << "\n";
        root = RootExpr::Or;
    } else if (exprs[0].as<Halide::Internal::Cast>()) {
        debug(5) << "Returning Cast root table for type " << print_types(types) << "\n";
        root = RootExpr::Cast;
    }

    if (root != RootExpr::Unknown) {
        const vector<AssociativePattern> &table = get_ops_table_helper(types, root, exprs.size());
        debug(7) << "Table size: " << table.size() << "\n";
        for (const auto &p : table) {
            debug(7) << p;
        }
        return table;
    }
    debug(5) << "Returning empty table\n";
    return empty;
}

}  // namespace Internal
}  // namespace Halide
