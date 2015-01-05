#include <map>
#include <string>
#include <vector>

#include "InlineReductions.h"
#include "IREquality.h"
#include "Matrix.h"
#include "Schedule.h"
#include "Simplify.h"
#include "Tuple.h"
#include "Util.h"

namespace Halide {

static const int small_matrix_limit = 4;

using namespace Internal;
using std::map;
using std::string;
using std::vector;

namespace {

bool is_int(Expr i) {
    return i.type().is_int() || i.type().is_uint();
}

bool is_size_const(Expr i) {
    bool valid = is_const(i) && is_int(i);
    if (valid) {
        const int n = *as_const_int(i);
        valid = n >= 0;
    }

    return valid;
}

string strip(string name) {
    int pos = name.find('$');
    return name.substr(0, pos);
}

string block_var_name(string base_name, int level) {
    std::ostringstream sout;
    sout << "b" << level << "_" << base_name;
    return sout.str();
}

string matrix_name(Matrix *M, string name, string alt_name = "") {
    static string default_name = make_entity_name(M, "Halide::Matrix", 'M');

    if (name.empty()) {
        if (alt_name.empty()) {
            return default_name;
        } else {
            return strip(alt_name);
        }
    } else {
        return strip(name);
    }
}

const vector<string> &matrix_args() {
    static const vector<string> args = vec(string("i"), string("j"));
    return args;
}

// Inject a suitable base-case definition given an update
// definition. This is a helper for MatrixRef::operator+= and co.
void define_base_case(Function func, const vector<Expr> &a, Expr e) {
    if (func.has_pure_definition()) return;
    func.define(matrix_args(), vec(e));
}

}

MatrixRef::MatrixRef(Matrix& M, Expr i, Expr j) : mat(M), row(i), col(j) {
    internal_assert(i.defined() && is_int(i));
    internal_assert(j.defined() && is_int(j));
}

void MatrixRef::operator=(Expr x) {
    if (mat.is_large) {
        if (mat.func.has_pure_definition()) {
            mat.define_update(row, col, x);
        } else {
            mat.define(x);
        }
    } else {
        const int i = mat.small_offset(row, col);
        mat.coeffs[i] = x;
    }
}

void MatrixRef::operator+=(Expr x) {
    if (mat.is_large) {
        define_base_case(mat.func, vec(row, col), cast(x.type(), 0));
        Expr value = *this;
        mat.define_update(row, col, value + x);
    } else {
        const int i = mat.small_offset(row, col);
        mat.coeffs[i] = mat.coeffs[i] + x;
    }
}

void MatrixRef::operator-=(Expr x) {
    if (mat.is_large) {
        define_base_case(mat.func, vec(row, col), cast(x.type(), 0));
        Expr value = *this;
        mat.define_update(row, col, value - x);
    } else {
        const int i = mat.small_offset(row, col);
        mat.coeffs[i] = mat.coeffs[i] - x;
    }
}

void MatrixRef::operator*=(Expr x) {
    if (mat.is_large) {
        define_base_case(mat.func, vec(row, col), cast(x.type(), 1));
        Expr value = *this;
        mat.define_update(row, col, value * x);
    } else {
        const int i = mat.small_offset(row, col);
        mat.coeffs[i] = mat.coeffs[i] * x;
    }
}

void MatrixRef::operator/=(Expr x) {
    if (mat.is_large) {
        define_base_case(mat.func, vec(row, col), cast(x.type(), 1));
        Expr value = *this;
        mat.define_update(row, col, value / x);
    } else {
        const int i = mat.small_offset(row, col);
        mat.coeffs[i] = mat.coeffs[i] / x;
    }
}

void MatrixRef::operator=(const MatrixRef &e) {
    (*this) = Expr(e);
}

void MatrixRef::operator=(const FuncRefVar &e) {
    internal_assert(e.size() == 1);
    (*this) = Expr(e);
}

void MatrixRef::operator=(const FuncRefExpr &e) {
    internal_assert(e.size() == 1);
    (*this) = Expr(e);
}

MatrixRef::operator Expr() const {
    if (mat.is_large) {
        return Call::make(mat.func, vec(row, col));
    } else {
        const int i = mat.small_offset(row, col);
        return mat.coeffs[i];
    }
}

struct PartitionContents : public RefCount {
    // Partitions are stored in a doubly-linked list hierachy, with the root being the lowest
    // level, representing individual coefficients of the matrix, and the following levels
    // corresponding to blocks of the matrix.
    IntrusivePtr<PartitionContents> prev;
    IntrusivePtr<PartitionContents> next;

    // Name of the matrix that this partition is applied to.
    string name;

    // A reference to the schedule for this level of the partition hierarchy.
    Schedule schedule;

    // Block variables, indexing the blocks at this level of the partition.
    Var  bi, bj;

    // The number of rows and columns in the matrix that we are partitioning.
    Expr mat_rows, mat_cols;

    // The number of rows and columns per block in this partition.
    Expr par_rows, par_cols;

    // The minimum number of rows and columns we must have in the
    // matrix in order to use this partition.
    Expr min_rows, min_cols;

    mutable RefCount ref_count;

    PartitionContents() : bi("i"), bj("j"), par_rows(1), par_cols(1),
                          min_rows(1), min_cols(1)
    {}
};

namespace Internal {

template<>
EXPORT RefCount &ref_count<PartitionContents>(const PartitionContents *p) {
    return p->ref_count;
}

template<>
EXPORT void destroy<PartitionContents>(const PartitionContents *p) {
    delete p;
}

}

Partition::Partition(IntrusivePtr<PartitionContents> c) :
        contents(c)
{}

Partition::Partition(Schedule schedule, const string &name, Expr m, Expr n) :
        contents(new PartitionContents) {
    contents.ptr->schedule = schedule;
    contents.ptr->name = name;
    contents.ptr->mat_rows = m;
    contents.ptr->mat_cols = n;
}

Partition::Partition(Partition p, Expr m, Expr n) :
        contents(new PartitionContents) {
    PartitionContents *prev_contents = p.contents.ptr;
    prev_contents->next = contents;

    contents.ptr->prev = p.contents;
    contents.ptr->name = prev_contents->name;
    contents.ptr->par_rows = m;
    contents.ptr->par_cols = n;
    contents.ptr->min_rows = simplify(m * prev_contents->min_rows);
    contents.ptr->min_cols = simplify(n * prev_contents->min_cols);
    contents.ptr->mat_rows = prev_contents->mat_rows;
    contents.ptr->mat_cols = prev_contents->mat_cols;

    int lvl = level();
    contents.ptr->bi = Var(block_var_name("i", lvl));
    contents.ptr->bj = Var(block_var_name("j", lvl));

    // std::cout << "Partitioning matrix " << prev_contents->name << " at level " << lvl
    //           << " into " << contents.ptr->par_rows << " x " << contents.ptr->par_cols << " blocks. "
    //           << "Partition vars: (" << contents.ptr->bi.name() << ", " << contents.ptr->bj.name() << ")\n"
    //           << "\tPartion blocks total size = " << contents.ptr->min_rows << " x " << contents.ptr->min_cols << "\n";

    Expr condition = contents.ptr->mat_rows >= contents.ptr->min_rows &&
            contents.ptr->mat_cols >= contents.ptr->min_cols;

    // std::cout << "\tPartition schedule is specialized on the condition:\n\t\t"
    //           << condition << "\n";

    // I do not think that the condtion should ever have been previously specialized, however,
    // I am copying the code from Func.cpp that searches for existing specializations below.
    // Just in case I need to revisit this.
    //
    // for (size_t i = 0; i < schedule.specializations().size(); i++) {
    //     if (equal(condition, schedule.specializations()[i].condition)) {
    //         return Stage(schedule.specializations()[i].schedule, stage_name);
    //     }
    // }

    const Specialization &s = prev_contents->schedule.add_specialization(condition);
    contents.ptr->schedule = s.schedule;

    Var bi = contents.ptr->bi;
    Var bj = contents.ptr->bj;
    Var pi = prev_contents->bi;
    Var pj = prev_contents->bj;

    // In order to allow vectorization during partitioning, we cache the schedule's dims
    // before splitting. Then set all of the for_type entries to serial during the split,
    // and finally restore their for_types after we are done;
    map<string, For::ForType> cached_dims;
    std::vector<Dim> &dims =  contents.ptr->schedule.dims();
    for (size_t i = 0; i < dims.size(); ++i) {
        std::string name = base_name(dims[i].var);
        // debug(0) << "\tCaching loop type of var: " << name << " type = "
        //          << (dims[i].for_type == For::Serial? "serial" :
        //             (dims[i].for_type == For::Parallel? "parallel" :
        //             (dims[i].for_type == For::Vectorized? "vectorized" :
        //              "unrolled"))) << "\n";
        cached_dims[name] = dims[i].for_type;
        dims[i].for_type = For::Serial;
    }

    // std::cout << "\tPartition tiled as: " << name() << "("
    //           << pi.name() << ", " << pj.name() << ", "
    //           << bi.name() << ", " << bj.name() << ", "
    //           << pi.name() << ", " << pj.name() << ", "
    //           << m << ", " << n << ")\n";
    schedule().tile(pi, pj, bi, bj, pi, pj, m, n);

    // Restore the for_type value for each dim in the schedule.
    for (size_t i = 0; i < dims.size(); ++i) {
        std::string name = base_name(dims[i].var);
        // debug(0) << "\tAttempting to restore loop type for dim: " << name << "\n";
        if (cached_dims.count(name) > 0) {
            dims[i].for_type = cached_dims[name];
            // debug(0) << "\tRestored cached loop type of var: " << name << " type = "
            //          << (dims[i].for_type == For::Serial? "serial" :
            //             (dims[i].for_type == For::Parallel? "parallel" :
            //             (dims[i].for_type == For::Vectorized? "vectorized" :
            //              "unrolled"))) << "\n";
        }
    }
}

int Partition::level() {
    if (is_root()) {
        return 0;
    } else {
        return parent().level() + 1;
    }
}

int Partition::depth() {
    int d = level();
    IntrusivePtr<PartitionContents> p = contents.ptr;
    while(p.defined()) {
        p = p.ptr->next;
        ++d;
    }
    return d;
}

Partition Partition::get_level(int n) {
    int lvl = level();

    std::cout << "\tgetting partition level " << n << ". current level = " << lvl << "\n";

    IntrusivePtr<PartitionContents> p = contents;
    while (p.defined() && n < lvl) {
        p = p.ptr->prev;
        --lvl;
    }

    while (p.defined() && n > lvl) {
        p = p.ptr->next;
        ++lvl;
    }

    std::cout << "\tresult contents @ " << p.ptr << "\n";

    return Partition(p);
}

Partition Partition::get_root() {
    return get_level(0);
}

Partition Partition::get_leaf() {
    return get_level(depth()-1);
}

const string &Partition::name() const {
    return contents.ptr->name;
}

Stage Partition::schedule() {
    return Stage(contents.ptr->schedule, contents.ptr->name);
}

Partition Partition::parent() {
    internal_assert(contents.defined()) << "Can't get parent\n";
    IntrusivePtr<PartitionContents> prev_contents = contents.ptr->prev;
    internal_assert(prev_contents.defined());
    return Partition(prev_contents);
}

Partition Partition::child() {
    internal_assert(contents.defined()) << "Can't get child\n";
    IntrusivePtr<PartitionContents> next_contents = contents.ptr->next;
    internal_assert(next_contents.defined());
    return Partition(next_contents);
}

bool Partition::is_root() const {
    internal_assert(contents.defined()) << "Can't check root status\n";
    IntrusivePtr<PartitionContents> prev_contents = contents.ptr->prev;
    return !prev_contents.defined();
}

Expr Partition::num_rows() const {
    internal_assert(contents.defined()) << "Can't check number of rows\n";
    return contents.ptr->par_rows;
}

Expr Partition::num_cols() const {
    internal_assert(contents.defined()) << "Can't check number of columns\n";
    return contents.ptr->par_cols;
}

Var Partition::row_var() const {
    internal_assert(contents.defined()) << "Can't get row var\n";
    return contents.ptr->bi;
}

Var Partition::col_var() const {
    internal_assert(contents.defined()) << "Can't get column var\n";
    return contents.ptr->bj;
}

void Partition::rename_row(Var v) {
    if (!v.same_as(row_var())) {
        schedule().rename(row_var(), v);
        contents.ptr->bi = v;
    }
}

void Partition::rename_col(Var v) {
    if (!v.same_as(col_var())) {
        schedule().rename(col_var(), v);
        contents.ptr->bj = v;
    }
}

Partition Partition::partition(Expr n) {
    return Partition(get_leaf(), n, n);
}

Partition Partition::partition(Expr m, Expr n) {
    return Partition(get_leaf(), m, n);
}

Partition &Partition::vectorize() {
    // std::cout << "\tvectorizing variable: " << row_var().name() << "\n";
    internal_assert(!is_root());
    user_assert(is_const(parent().num_rows())) <<
            "When vectorizing a partition level of a matrix, you must vectorize at a level " <<
            "with a constant number of rows. You have attempted to vectorize, at level " << level() << ", "
            "which has " << parent().num_rows() << " rows per block of the partition.\n";
    schedule().vectorize(parent().row_var());
    return *this;
}

Partition &Partition::unroll_rows() {
    // std::cout << "\tunrolling row variable: " << row_var().name() << "\n";
    internal_assert(!is_root());
    user_assert(is_const(parent().num_rows())) <<
            "When unrolling the rows of a partition level of a matrix, you must unroll at a level " <<
            "with a constant number of rows. You have attempted to unroll, at level " << level() << ", "
            "which has " << parent().num_rows() << " rows per block of the partition.\n";
    schedule().unroll(parent().row_var());
    return *this;
}

Partition &Partition::unroll_cols() {
    // std::cout << "\tunrolling col variable: " << col_var().name() << "\n";
    internal_assert(!is_root());
    user_assert(is_const(parent().num_cols())) <<
            "When unrolling the columns of a partition level of a matrix, you must unroll at a level " <<
            "with a constant number of columns. You have attempted to unroll, at level " << level() << ", "
            "which has " << parent().num_cols() << " columns per block of the partition.\n";
    schedule().unroll(parent().col_var());
    return *this;
}

Partition &Partition::parallel_rows() {
    std::cout << "\tparallelizing variable: " << row_var().name() << "\n";
    schedule().parallel(row_var());
    return *this;
}

Partition &Partition::parallel_cols() {
    std::cout << "\tparallelizing variable: " << col_var().name() << "\n";
    schedule().parallel(col_var());
    return *this;
}


int Matrix::small_offset(Expr row, Expr col) const {
    if (!is_large) {
        internal_assert(is_size_const(row));
        internal_assert(is_size_const(col));
        internal_assert(is_size_const(nrows));
        internal_assert(is_size_const(ncols));

        const int i = *as_const_int(row);
        const int j = *as_const_int(col);
        const int m = *as_const_int(nrows);

        return i + j * m;
    }

    return -1;
}

void Matrix::init(Expr num_rows = 0, Expr num_cols = 0) {
    partitions.push_back(Partition(func.schedule(), func.name(), num_rows, num_cols));

    nrows = num_rows;
    ncols = num_cols;

    internal_assert(nrows.defined() && is_int(nrows));
    internal_assert(ncols.defined() && is_int(ncols));

    is_large = true;

    int m, n;
    if(const_size(m, n)) {
        if (m <= small_matrix_limit &&
            n <= small_matrix_limit ) {
            is_large = false;
        }
    }

    vec_level = -1;
    row_loop_types.resize(1, For::Serial);
    col_loop_types.resize(1, For::Serial);
}

void Matrix::define(Expr value) {
    func.define(matrix_args(), vec(value));
}

void Matrix::define_update(Expr row, Expr col, Expr value) {
    func.define_update(vec(row, col), vec(value));
    int idx = partitions.size() - 1;
    partitions.push_back(Partition(func.update_schedule(idx), func.name(), nrows, ncols));
}

bool Matrix::const_num_rows(int &m) {
    if (is_size_const(nrows)) {
        m = *as_const_int(nrows);
        return true;
    } else {
        return false;
    }
}

bool Matrix::const_num_cols(int &n) {
    if (is_size_const(ncols)) {
        n = *as_const_int(ncols);
        return true;
    } else {
        return false;
    }
}

bool Matrix::const_size(int &m, int &n) {
    bool is_const = const_num_rows(m) && const_num_cols(n);
    return is_const;
}

// Stage Matrix::root_schedule(int update) {
//     internal_assert(func.defined());

//     if (update < 0) {
//         return static_cast<Stage>(func);
//     } else {
//         return func.update(update);
//     }
// }

Matrix::Matrix(string name) :
        func(matrix_name(this, name)) {
    init(0, 0);
}

Matrix::Matrix(Expr num_row, Expr num_col, Type t, string name) :
        func(matrix_name(this, name)) {
    init(num_row, num_col);

    if (!is_large) {
        int m, n;
        const_size(m, n);
        coeffs.resize(m * n, Halide::undef(t));
    }
}

Matrix::Matrix(Expr num_row, Expr num_col, Tuple c, string name)
        : func(matrix_name(this, name)) {
    init(num_row, num_col);
    internal_assert(!is_large);

    int m, n;
    const_size(m, n);
    internal_assert((size_t)(m * n) == c.size());

    Type t = c[0].type();
    coeffs.resize(m * n);
    for (size_t i = 0; i < c.size(); ++i) {
        internal_assert(c[i].type() == t);
        coeffs[i] = c[i];
    }
}

Matrix::Matrix(Expr num_row, Expr num_col, vector<Expr> c, string name)
        : func(matrix_name(this, name)) {
    init(num_row, num_col);
    internal_assert(!is_large);

    int m, n;
    const_size(m, n);
    internal_assert((size_t)(m * n) == c.size());

    Type t = c[0].type();
    coeffs.resize(m * n);
    for (size_t i = 0; i < c.size(); ++i) {
        internal_assert(c[i].type() == t);
        coeffs[i] = c[i];
    }
}

Matrix::Matrix(ImageParam img, string name)
        : func(matrix_name(this, name, img.name())) {
    if (img.dimensions() == 1) {
        init(img.width(), 1);

        if (is_large) {
            Var i = row_var();
            Var j = col_var();
            Matrix &A = *this;
            A(i, j) = img(i);
        } else {
            int m, n;
            const_size(m, n);
            coeffs.resize(m);  // n == 1
            for (int i = 0; i < m; ++i) {
                coeffs[i] = img(i);
            }
        }
    } else {
        internal_assert(img.dimensions() == 2);

        init(img.width(), img.height());

        if (is_large) {
            Var i = row_var();
            Var j = col_var();
            Matrix &A = *this;
            A(i, j) = img(i, j);
        } else {
            int m, n;
            const_size(m, n);
            coeffs.resize(m * n);

            for (int j = 0; j < n; ++j) {
                for (int i = 0; i < m; ++i) {
                    const int idx = small_offset(i, j);
                    coeffs[idx] = img(i, j);
                }
            }
        }
    }
}

Matrix::Matrix(Expr num_row, Expr num_col, Func f, string name)
        : func(matrix_name(this, name, f.name())) {
    internal_assert(f.outputs() == 1);

    init(num_row, num_col);

    if (f.dimensions() == 1) {
        internal_assert(is_one(ncols) || is_one(nrows));

        if (!is_large) {
            int m, n;
            const_size(m, n);
            coeffs.resize(m * n);
            for (int i = 0; i < m * n; ++i) {
                coeffs[i] = f(i);
            }
        } else if (is_one(ncols)) {
            Var i = row_var();
            Var j = col_var();
            Matrix &A = *this;
            A(i, j) = f(i);
        } else {// is_one(nrows)
            Var i = row_var();
            Var j = col_var();
            Matrix &A = *this;
            A(i, j) = f(j);
        }
    } else {
        internal_assert(f.dimensions() == 2);

        if (is_large) {
            Var i = row_var();
            Var j = col_var();
            Matrix &A = *this;
            A(i, j) = f(i, j);
        } else {
            int m, n;
            const_size(m, n);
            coeffs.resize(m * n);
            for (int j = 0; j < n; ++j) {
                for (int i = 0; i < m; ++i) {
                    int idx = small_offset(i, j);
                    coeffs[idx] = f(i, j);
                }
            }
        }
    }
}

Var Matrix::row_var() const {
    return get_partition().row_var();
}

Var Matrix::col_var() const {
    return get_partition().col_var();
}

Type Matrix::type() const {
    if (is_large) {
        return func.output_types()[0];
    } else {
        return coeffs[0].type();
    }
}

Expr Matrix::num_rows() const {
    return nrows;
}

Expr Matrix::num_cols() const {
    return ncols;
}

Matrix::operator Tuple() {
    internal_assert(!is_large);
    return Tuple(coeffs);
}

Matrix::operator Func() {
    if (!is_large && !func.has_pure_definition()) {
        int m, n;
        const_size(m, n);

        Expr mat = undef(type());
        for (int j = 0; j < n; ++j ) {
            for (int i = 0; i < n; ++i ) {
                const int idx = small_offset(i, j);
                mat = select(row_var() == i && col_var() == j,
                             coeffs[idx], mat);
            }
        }

        func.define(matrix_args(), vec(mat));
    }

    return Func(func);
}

Matrix &Matrix::compute_at_rows(Partition p) {
    if (is_large) {
        // Inject the compute_at variable into all other branches of the
        // specialization tree via renames.
        Partition q = p;
        bool done = false;
        while (!done) {
            // std::cout << "Renaming row var in partition " << q.level()
            //           << ": " << q.row_var() << " --> " << p.row_var() << "\n";
            q.rename_row(p.row_var());
            if (q.is_root()) {
                done = true;
            } else {
                q = q.parent();
            }
        }

        LoopLevel loop_level(p.name(), p.row_var().name());
        func.schedule().compute_level() = loop_level;
        if (func.schedule().store_level().is_inline()) {
            func.schedule().store_level() = loop_level;
        }
    }

    return *this;
}

Matrix &Matrix::compute_at_columns(Partition p) {
    if (is_large) {
        // Inject the compute_at variable into all other branches of the
        // specialization tree via renames.
        Partition q = p;
        bool done = false;
        while (!done) {
            // std::cout << "Renaming col var in partition " << q.level()
            //           << ": " << q.col_var() << " --> " << p.col_var() << "\n";
            q.rename_col(p.col_var());
            if (q.is_root()) {
                done = true;
            } else {
                q = q.parent();
            }
        }

        LoopLevel loop_level(p.name(), p.col_var().name());
        func.schedule().compute_level() = loop_level;
        if (func.schedule().store_level().is_inline()) {
            func.schedule().store_level() = loop_level;
        }
    }

    return *this;
}

// Matrix &Matrix::compute_at_blocks(Matrix &other, Expr block_rows, Expr block_cols) {
//     LoopLevel loop_level(other.name(), col_var().name());
//     schedule.compute_level() = loop_level;
//     if (schedule.store_level().is_inline()) {
//         schedule.store_level() = loop_level;
//     }

//     if (is_large) {
//         func.schedule() = schedule;
//     }
// }

// Buffer Matrix::realize() {
//   internal_assert(is_size_const(nrows));
//   internal_assert(is_size_const(ncols));

//   const int nr = *as_const_int(nrows);
//   const int nc = *as_const_int(ncols);

//   Func f = *this;
//   f.bound(x, 0, nrows).bound(y, 0, ncols);

//   return f.realize(nr, nc);
// }

Matrix &Matrix::partition(Expr size) {
    return partition(size, size);
    return *this;
}

Matrix &Matrix::partition(Expr row_size, Expr col_size) {
    Partition p = get_partition().partition(row_size, col_size);

    int level = p.level();
    if (level == vec_level) {
        const int *vec_size = as_const_int(row_size);
        user_assert(vec_size) <<
                "Attemtping to vectorize matrix computations on a partition that doesn't "
                "have a constant number of rows. Partition at level " << level << " on "
                "matrix " << name() << " does not have constant height.\n";

        p.vectorize();
    }

    return *this;
}

Matrix &Matrix::vectorize(int level) {
    user_assert(vec_level == -1) <<
            "You may only schedule one level of a matrix partition with vectorize, "
            "you have already called vectorize on matrix " << name() << " at "
            "level " << vec_level << ".\n";

    Partition p = get_partition();
    if (level < 0) {
        vec_level = p.depth()-1;
    } else {
        vec_level = level;
    }

    if ((int)row_loop_types.size() <= vec_level) {
        row_loop_types.resize(level+1, For::Serial);
    }
    row_loop_types[vec_level] = For::Vectorized;

    if (vec_level < p.depth()) {
        p = p.get_level(vec_level);

        const int *vec_size = as_const_int(p.num_rows());
        user_assert(vec_size) <<
                "Attemtping to vectorize matrix computations on a partition that doesn't "
                "have a constant number of rows. Partition at level " << level << " on "
                "matrix " << name() << " does not have constant height.\n";

        p.vectorize();
    }

    return *this;
}

Matrix &Matrix::unroll_rows(int level) {
    // user_assert(par_level == -1) <<
    //         "You may only schedule one level of a matrix partition with unroll, "
    //         "you have already called unrollize on matrix " << name() << " at "
    //         "level " << par_level << ".\n";

    Partition p = get_partition();
    if (level < 0) {
        level = p.depth();
    }

    if ((int)row_loop_types.size() <= level) {
        row_loop_types.resize(level+1, For::Serial);
    }
    row_loop_types[level] = For::Unrolled;

    if (level < p.depth()) {
        p = p.get_level(level);
        p.unroll_rows();
    }

    return *this;
}

Matrix &Matrix::unroll_cols(int level) {
    // user_assert(par_level == -1) <<
    //         "You may only schedule one level of a matrix partition with unroll, "
    //         "you have already called unrollize on matrix " << name() << " at "
    //         "level " << par_level << ".\n";

    Partition p = get_partition();
    if (level < 0) {
        level = p.depth();
    }

    if ((int)col_loop_types.size() <= level) {
        col_loop_types.resize(level+1, For::Serial);
    }
    col_loop_types[level] = For::Unrolled;

    if (level < p.depth()) {
        p = p.get_level(level);
        p.unroll_cols();
    }

    return *this;
}

Matrix &Matrix::parallel_rows(int level) {
    // user_assert(par_level == -1) <<
    //         "You may only schedule one level of a matrix partition with parallel, "
    //         "you have already called parallelize on matrix " << name() << " at "
    //         "level " << par_level << ".\n";

    Partition p = get_partition();
    if (level < 0) {
        level = p.depth()-1;
    }

    if ((int)row_loop_types.size() <= level) {
        row_loop_types.resize(level+1, For::Serial);
    }
    row_loop_types[level] = For::Parallel;

    if (level < p.depth()) {
        p = p.get_level(level);
        p.parallel_rows();
    }

    return *this;
}

Matrix &Matrix::parallel_cols(int level) {
    // user_assert(par_level == -1) <<
    //         "You may only schedule one level of a matrix partition with parallel, "
    //         "you have already called parallelize on matrix " << name() << " at "
    //         "level " << par_level << ".\n";

    Partition p = get_partition();
    if (level < 0) {
        level = p.depth()-1;
    }

    if ((int)col_loop_types.size() <= level) {
        col_loop_types.resize(level+1, For::Serial);
    }
    col_loop_types[level] = For::Parallel;

    if (level < p.depth()) {
        p = p.get_level(level);
        p.parallel_cols();
    }

    return *this;
}

Partition &Matrix::get_partition(int update) {
    internal_assert(0 <= update && update < (int)partitions.size());
    return partitions[update];
}

const Partition &Matrix::get_partition(int update) const {
    internal_assert(0 <= update && update < (int)partitions.size());
    return partitions[update];
}

Matrix Matrix::block(Expr min_i, Expr max_i, Expr min_j, Expr max_j) {
    Matrix &A = *this;

    string result_name = strip(this->name()) + "_block";

    Expr block_nrows = simplify(max_i - min_i + 1);
    Expr block_ncols = simplify(max_j - min_j + 1);

    if (!is_large) {
        int m, n;
        const_size(m, n);

        vector<Expr> block_coeffs(m * n);
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < m; ++i) {
                const int idx = i + j * m;
                block_coeffs[idx] = static_cast<Expr>(A(i, j));
            }

            return Matrix(m, n, block_coeffs, result_name);
        }
    }

    Matrix block(block_nrows, block_ncols, A.type(), result_name);
    block(row_var(), col_var()) =
            Halide::select(row_var() <= block_nrows && col_var() <= block_ncols,
                           A(row_var() - min_i, col_var() - min_j),
                           Halide::undef(type()));
    return block;
}

Matrix Matrix::row(Expr i) {
    Matrix &A = *this;

    string result_name = strip(this->name()) + "_block";

    if (!is_large) {
        int m, n;
        const_size(m, n);

        vector<Expr> row_coeffs(m * n);
        for (int j = 0; j < n; ++j) {
            row_coeffs[j] = static_cast<Expr>(A(i, j));
        }

        return Matrix(m, n, row_coeffs, result_name);
    }

    Matrix row(1, ncols, A.type(), result_name);
    row(row_var(), col_var()) = A(0, col_var());
    return row;
}

Matrix Matrix::col(Expr j) {
    Matrix &A = *this;

    string result_name = strip(this->name()) + "_col";

    int m;
    if (const_num_rows(m)) {
        if (m <= 4) {
            vector<Expr> col_coeffs(m);
            for (int i = 0; i < m; ++i) {
                col_coeffs[i] = static_cast<Expr>(A(i, j));
            }

            return Matrix(nrows, 1, col_coeffs, result_name);
        }
    }

    Matrix col(nrows, 1, A.type(), result_name);
    col(row_var(), col_var()) = A(row_var(), 0);
    return col;
}

Matrix Matrix::transpose() {
    string result_name = strip(this->name()) + "_t";

    if (is_large) {
        Matrix &A = *this;
        Matrix A_t(ncols, nrows, A.type(), result_name);
        A_t(row_var(), col_var()) = A(col_var(), row_var());
        return A_t;
    } else {
        const int m = *as_const_int(nrows);
        const int n = *as_const_int(ncols);

        vector<Expr> coeff_trans(m * n);
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < m; ++i) {
                const int idx = small_offset(i, j);
                const int idx_t = small_offset(j, i);
                coeff_trans[idx_t] = coeffs[idx];
            }
        }
        return Matrix(ncols, nrows, coeff_trans, result_name);
    }
}

Expr Matrix::cofactor(int i, int j) {
    user_assert(!is_large)
            << "matrix cofactors are only available for small matrices.\n";

    const int m = *as_const_int(nrows);
    const int n = *as_const_int(ncols);
    user_assert(m == n)
            << "matrix cofactors are only defined for square matrices.\n";

    Matrix &A = *this;
    Matrix  B(n-1, n-1, A.type());
    Expr sign = (i + j) % 2 == 0? 1 : -1;

    for (int k = 0; k < n-1; ++k) {
        const int k_off = k < j? 0: 1;
        for (int l = 0; l < n-1; ++l) {
            const int l_off = l < i? 0: 1;
            B(l,k) = A(l + l_off, k + k_off);
        }
    }

    return sign * B.determinant();
}

Expr Matrix::determinant() {
    user_assert(!is_large)
            << "matrix determinant is only available for small matrices.\n";

    // Assert nrows == ncols!!!
    const int m = *as_const_int(nrows);
    const int n = *as_const_int(ncols);
    user_assert(m == n)
            << "matrix determinant is only defined for square matrices.\n";

    Matrix &A = *this;

    Expr det = cast(type(), 0);
    if (n == 1) {
        det = A(0,0);
    } else if (n == 2) {
        det = A(0,0)*A(1,1) - A(0,1)*A(1,0);
    } else if (n == 3) {
        det = A(0,0)*(A(1,1)*A(2,2) - A(1,2)*A(2,1))
                - A(0,1)*(A(1,0)*A(2,2) - A(1,2)*A(2,0))
                + A(0,2)*(A(1,0)*A(2,1) - A(1,1)*A(2,0));
    } else { /*if (n == 4)*/
        for (int j = 0; j < n; ++j) {
            det += A(0,j) * A.cofactor(0, j);
        }
    }
    return det;
}

Matrix Matrix::inverse() {
    user_assert(!is_large)
            << "matrix inverse is only available for small matrices.\n";

    // Assert nrows == ncols!!!
    const int m = *as_const_int(nrows);
    const int n = *as_const_int(ncols);
    user_assert(m == n)
            << "matrix inverse is only defined for square matrices.\n";

    Matrix &A = *this;
    Expr det = A.determinant();

    Matrix inv(n, n, type());
    if (n == 1) {
        inv(0,0) = 1 / A(0,0);
    } else if (n == 2) {
        inv(0,0) =  A(1,1) / det;  inv(0,1) = -A(0,1) / det;
        inv(1,0) = -A(1,0) / det;  inv(1,1) =  A(0,0) / det;
    } else { /*if (n == 3 || n == 4)*/
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                inv(i, j) = cofactor(j, i) / det;
            }
        }
    }
    return inv;
}

MatrixRef Matrix::operator[] (Expr i) {
    user_assert(is_one(nrows) || is_one(ncols))
            << "operator[] only defined for 1-dimensional matrices.\n";

    if (is_one(nrows)) {
        return MatrixRef(*this, 0, i);
    } else /*if (is_one(ncols))*/ {
        return MatrixRef(*this, i, 0);
    }
}

MatrixRef Matrix::operator() (Expr i, Expr j) {
    return MatrixRef(*this, i, j);
}

Matrix identity_matrix(Type t, Expr size) {
    if (is_positive_const(size)) {
        const int n = *as_const_int(size);

        if (n <= 4) {
            Tuple ident(vector<Expr>(n*n));
            for (int j = 0; j < n; ++j) {
                for (int i =0; i < n; ++i) {
                    const int idx = i + j * n;
                    ident[idx] = i == j? cast(t, 1): cast(t, 0);
                }
            }

            return Matrix(size, size, ident, "I");
        }
    }

    Matrix I(size, size, t, "I");
    Var i = I.row_var();
    Var j = I.col_var();
    I(i, j) = select(i == j, cast(t, 1), cast(t, 0));
    return I;
}

Matrix operator+(Matrix A, Matrix B) {
    user_assert(equal(A.num_rows(), B.num_rows()) ||
                equal(A.num_cols(), B.num_cols())) <<
            "Attempting to add matrices of different sizes.";

    string result_name = strip(A.name()) + "_plus_" + strip(B.name());

    if (A.is_large_matrix()) {
        Matrix sum(A.num_rows(), A.num_cols(), A.type(), result_name);
        Var i = sum.row_var();
        Var j = sum.col_var();
        sum(i, j) = A(i, j) + B(i, j);
        return sum;
    } else {
        Tuple sum = A;
        Tuple B_coeffs = B;
        for (size_t k = 0; k < sum.size(); ++k) {
            sum[k] += B_coeffs[k];
        }

        return Matrix(A.num_rows(), A.num_cols(), sum, result_name);
    }
}

Matrix operator-(Matrix A, Matrix B) {
    user_assert(equal(A.num_rows(), B.num_rows()) ||
                equal(A.num_cols(), B.num_cols())) <<
            "Attempting to subtract matrices of different sizes.";

    string result_name = strip(A.name()) + "_minus_" + strip(B.name());

    if (A.is_large_matrix()) {
        Matrix diff(A.num_rows(), A.num_cols(), A.type(), result_name);
        Var i = diff.row_var();
        Var j = diff.col_var();
        diff(i, j) = A(i, j) - B(i, j);
        return diff;
    } else {
        Tuple diff = A;
        Tuple B_coeffs = B;
        for (size_t k = 0; k < diff.size(); ++k) {
            diff[k] -= B_coeffs[k];
        }

        return Matrix(A.num_rows(), A.num_cols(), diff, result_name);
    }
}

Matrix operator*(Expr a, Matrix B) {
    string result_name = strip(B.name()) + "_scaled";

    if (B.is_large_matrix()) {
        Matrix scale(B.num_rows(), B.num_cols(), B.type(), result_name);
        Var i = scale.row_var();
        Var j = scale.col_var();
        scale(i, j) = a * B(i, j);
        return scale;
    } else {
        Tuple scale = B;
        for (size_t k = 0; k < scale.size(); ++k) {
            scale[k] = a * scale[k];
        }

        return Matrix(B.num_rows(), B.num_cols(), scale, result_name);
    }
}

Matrix operator*(Matrix B, Expr a) {
    string result_name = strip(B.name()) + "_scaled";

    if (B.is_large_matrix()) {
        Matrix scale(B.num_rows(), B.num_cols(), B.type(), result_name);
        Var i = scale.row_var();
        Var j = scale.col_var();
        scale(i, j) = B(i, j) * a;
        return scale;
    } else {
        Tuple scale = B;
        for (size_t k = 0; k < scale.size(); ++k) {
            scale[k] *= a;
        }

        return Matrix(B.num_rows(), B.num_cols(), scale, result_name);
    }
}

Matrix operator/(Matrix B, Expr a) {
    string result_name = strip(B.name()) + "_scaled";

    if (B.is_large_matrix()) {
        Matrix scale(B.num_rows(), B.num_cols(), B.type(), result_name);
        Var i = scale.row_var();
        Var j = scale.col_var();
        scale(i, j) = B(i, j) / a;
        return scale;
    } else {
        Tuple scale = B;
        for (size_t k = 0; k < scale.size(); ++k) {
            scale[k] /= a;
        }

        return Matrix(B.num_rows(), B.num_cols(), scale, result_name);
    }
}

Matrix operator*(Matrix A, Matrix B) {
    // user_assert(equal(A.num_cols(), B.num_rows()))
    //         << "Attempting to multiply matrices of mis-matched dimensions: "
    //         << A.num_rows() << "x" << A.num_cols() << " and "
    //         << B.num_rows() << "x" << B.num_cols() << ".\n";

    Expr prod_nrows = A.num_rows();
    Expr prod_ncols = B.num_cols();

    string result_name = strip(A.name()) + "_times_" + strip(B.name());

    if (is_positive_const(prod_nrows) && is_positive_const(prod_ncols)) {
        const int m = *as_const_int(prod_nrows);
        const int n = *as_const_int(prod_ncols);

        if (m <= 4 && n <= 4) {
            // Product will be a small matrix.
            Tuple prod(vector<Expr> (m*n));

            for (int j = 0; j < n; ++j) {
                for (int i = 0; i < m; ++i) {
                    const int idx = i + j * m;
                    if (A.is_large_matrix()) {
                        RDom k(0, A.num_rows(), "k");
                        prod[idx] = sum(A(i, k) * B(k, j));
                    } else {
                        const int p = *as_const_int(A.num_cols());
                        prod[idx] = cast(A.type(), 0);
                        for (int k = 0; k < p; ++k) {
                            prod[idx] += A(i, k) * A(k, j);
                        }
                    }
                }
            }

            return Matrix(prod_nrows, prod_ncols, prod, result_name);
        }
    }

    const int  vec_size = 8;
    const int  l1_size = 2;
    const int  l2_size = 2;
    // const int  tile_size = 4;
    const int  block_size = 32;

    const Expr sum_size = A.num_cols();
    const Expr proxy_size = ((sum_size + block_size - 1) / block_size) * block_size;

    Var i = A.row_var();
    Var j = A.col_var();
#if 1
    Var bi("bi"), bj("bj");
    RDom k(0, proxy_size);
    RVar ki, kii;

    Func A_mat("A_mat");
    A_mat(i, j) = select(i < prod_nrows,
                  select(j < sum_size, A(i, j),
                         select(i == j, 1.0f, 0.0f)),
                         select(i == j, 1.0f, 0.0f));

    Func B_mat("B_mat");
    B_mat(i, j) = select(i < sum_size,
                  select(j < prod_ncols, B(i, j),
                         select(i == j, 1.0f, 0.0f)),
                         select(i == j, 1.0f, 0.0f));

    Func prod("prod");
    prod(i, j) += A_mat(i, k) * B_mat(k, j);

    Matrix C(prod_nrows, prod_ncols, prod, result_name);
    C.partition(vec_size).vectorize()
     .partition(l1_size)
     .partition(l2_size).parallel_cols();

    // prod.tile(i, j, bi, bj, block_size, block_size).parallel(j)
    //     .vectorize(bi, vec_size).unroll(bi);

    Partition base = C.get_partition().get_level(0);
    Partition vecs = C.get_partition().get_level(1);
    Partition l1 = C.get_partition().get_level(2);
    Partition l2 = C.get_partition().get_level(3);
    base.rename_row(l2.row_var());
    vecs.rename_row(l2.row_var());
    l1.rename_row(l2.row_var());
    prod.compute_at(C, l2.row_var())
        .vectorize(i, vec_size).unroll(i);
    prod.update(0)
        .split(k, k, ki, block_size)
        .split(ki, ki, kii, l2_size)
        .reorder(kii, i, j, ki, k)
        .vectorize(i, vec_size).unroll(i);

    A_mat.compute_at(prod, k).vectorize(i, vec_size).unroll(i);
    B_mat.compute_at(prod, k).vectorize(i, vec_size).unroll(i);

    // const int vec_size  = 32 / A.type().bytes();
    // const int tile_size = 4;
    // // const int block_size = vec_size * tile_size;
#else
    Matrix prod(prod_nrows, prod_ncols, A.type(), "prod");
    Matrix C(prod_nrows, prod_ncols, A.type(), result_name);

    RDom k(0, A.num_cols(), "k");
    prod(i, j) += A(i, k) * B(k, j);
    C(i, j) = prod(i, j);

    C.partition(block_size)/*.partition(4)*/.parallel_rows();
    prod.compute_at_rows(C.get_partition().get_level(1))
        .partition(vec_size).vectorize();

    Partition prod_part = prod.get_partition(1);
    prod_part.partition(vec_size).vectorize()//.unroll_cols()
             .partition(tile_size);//.unroll_rows();

    A.compute_at_rows(prod_part.get_level(2))
            .partition(vec_size, 1).vectorize();

    B.compute_at_rows(prod_part.get_level(2))
            .partition(vec_size, 1).vectorize();
#endif
    return C;
}


}
