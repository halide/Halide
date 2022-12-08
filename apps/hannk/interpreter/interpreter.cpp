#include "interpreter/interpreter.h"
#include "interpreter/allocation_planner.h"
#include "interpreter/transforms.h"
#include "util/error_util.h"

#include "HalideBuffer.h"  // for HALIDE_RUNTIME_BUFFER_ALLOCATION_ALIGNMENT
#include "HalideRuntime.h"

#include <map>
#include <set>
#include <unordered_set>

namespace hannk {

Interpreter::Interpreter(OpPtr m, InterpreterOptions options)
    : model_(std::move(m)), options_(std::move(options)) {
}

Interpreter::~Interpreter() {
}

namespace {

// TODO: maybe move this to a separate file? Not sure if it's complex enough to be worthy or not.
class TensorVisitor : public OpVisitor {
    using OpVisitor::visit;

    virtual void visit_tensor(const TensorPtr &t) = 0;

    void visit(const OpGroup *g) override {
        for (int i = 0; i < g->op_count(); i++) {
            op_index_++;
            const Op *op = g->op(i);
            for (int j = 0; j < op->input_count(); j++) {
                visit_tensor(op->input(j));
            }
            for (int j = 0; j < op->output_count(); j++) {
                visit_tensor(op->output(j));
            }
            op->accept(this);
        }
    }

    int op_index_ = -1;

public:
    int op_index() const {
        return op_index_;
    }
};

struct TensorAllocationInfo {
    size_t size_needed = 0;
    int first_use = std::numeric_limits<int>::max();
    int last_use = std::numeric_limits<int>::min();
    int block_index = -1;
    std::set<TensorPtr> tensors;
};

bool needs_arena_allocation(const TensorPtr &t) {
    if (!t || t->is_external() || t->is_dynamic() || t->is_allocated()) {
        return false;
    }
    return true;
}

class FindAllocatableTensors : public TensorVisitor {
    void visit_tensor(const TensorPtr &t) override {
        if (!needs_arena_allocation(t)) {
            return;
        }
        TensorStoragePtr storage = t->storage();
        assert(storage != nullptr);

        auto &info = tensor_info[storage];
        assert(info.size_needed == 0 || info.size_needed == storage->storage_size());

        info.size_needed = storage->storage_size();
        info.first_use = std::min(info.first_use, op_index());
        info.last_use = std::max(info.last_use, op_index());
        // leave block_index as -1
        info.tensors.insert(t);
    }

public:
    // Iteration order matters, so don't use unordered_map without consideration.
    std::map<TensorStoragePtr, TensorAllocationInfo> tensor_info;
};

std::unique_ptr<char[]> allocate_tensors(const Op *root, const InterpreterOptions &options) {
    // Find the tensors that we want to allocate in an arena,
    // along the needed storage size and lifetime for each.
    FindAllocatableTensors find_tensors;
    root->accept(&find_tensors);

    if (options.verbosity >= 1) {
        for (const auto &it : find_tensors.tensor_info) {
            const auto &info = it.second;
            HLOG(INFO) << "TensorStorage of size " << info.size_needed << " life [" << info.first_use << " ... " << info.last_use << "]";
            for (const auto &t : info.tensors) {
                HLOG(INFO) << "  Tensor: " << t->name() << " size " << t->buffer().size_in_bytes();
            }
        }
    }

    // Feed this info to the allocation planner.
    // Let's assume that whatever alignment halide_malloc() needs is necessary here, too.
    // (Note that TFLite will complain if alignment is less than 64...)
    // Let's assume that whatever alignment Halide::Runtime::Buffer needs is necessary here, too.
    constexpr int kTfLiteDefaultTensorAlignment = 64;
    constexpr int kHalideBufferAlignment = HALIDE_RUNTIME_BUFFER_ALLOCATION_ALIGNMENT;
    constexpr size_t alignment = (size_t)std::max(kHalideBufferAlignment, kTfLiteDefaultTensorAlignment);
    AllocationPlanner planner(alignment);
    for (auto &it : find_tensors.tensor_info) {
        auto &info = it.second;
        info.block_index = planner.add_block(info.size_needed, info.first_use, info.last_use);
        assert(info.block_index >= 0);
    }
    planner.commit();

    if (options.verbosity >= 1) {
        std::ostringstream oss;
        oss << "Arena memory needed: " << planner.memory_needed() << '\n';
        oss << "    Offsets:";
        for (int i = 0; i < planner.block_count(); i++) {
            oss << ' ' << planner.get_block_offset(i);
        }
        if (options.verbosity >= 2) {
            oss << "\nUsage Map:\n";
            planner.dump(oss);
        }
        HLOG(INFO) << oss.str();
    }

    // Allocate the chunk we need. Be sure to over-allocate for alignment.
    std::unique_ptr<char[]> arena(new char[planner.memory_needed() + alignment]);
    assert(arena != nullptr);

    // Point all the tensors at the correct offsets.
    char *arena_base = arena.get();

    // Make sure that the 'base' we start from is aligned.
    arena_base = (char *)(((uintptr_t)arena_base + alignment - 1) & ~(alignment - 1));

    for (const auto &it : find_tensors.tensor_info) {
        const auto &info = it.second;
        char *new_host = arena_base + planner.get_block_offset(info.block_index);
        for (const auto &t : info.tensors) {
            t->allocate_from_arena_pointer(new_host);
        }
    }

    return arena;
}

class VerifyAllAllocated : public TensorVisitor {
    void visit_tensor(const TensorPtr &t) override {
        if (!needs_arena_allocation(t)) {
            return;
        }
        assert(t->is_allocated());
    }
};

#ifndef NDEBUG

class Checker : public OpVisitor {
    using OpVisitor::visit;

    std::unordered_set<Tensor *> valid_tensors_;

    void check_tensors(const Op *op) {
        for (int j = 0; j < op->input_count(); j++) {
            Tensor *t = op->input(j).get();
            if (!t->is_constant() && !valid_tensors_.count(t)) {
                HLOG(ERROR) << "Op " << op->name() << " uses tensor " << op->input(j)->name() << " but it is not produced yet\n";
                correct = false;
                return;
            }
        }
        for (int j = 0; j < op->output_count(); j++) {
            valid_tensors_.insert(op->output(j).get());
        }
    }

    void visit_leaf(const Op *op) override {
        if (!correct) {
            return;
        }
        check_tensors(op);
    }

    void visit(const OpGroup *op) override {
        if (!correct) {
            return;
        }
        check_tensors(op);
        OpVisitor::visit(op);
    }

public:
    explicit Checker(const Op *root) {
        for (int j = 0; j < root->input_count(); j++) {
            valid_tensors_.insert(root->input(j).get());
        }
    }

    bool correct = true;
};

// Verify that no Op comes before any of its input Tensors are produced.
void do_check_op_order(const Op *root) {
    Checker checker(root);
    root->accept(&checker);
    if (!checker.correct) {
        HCHECK(0) << "The model is not in the correct order.";
    }
}
#endif

}  // namespace

bool Interpreter::prepare() {
    if (prepared_) {
        HLOG(ERROR) << "Do not call prepare() twice";
        return false;
    }

    // We must prepare the model before doing the transforms, as some of the
    // transforms may rely on information cached by prepare(), e.g. alignment requirements.
    // (Note that any transforms that add new ops are expected to call prepare() on them,
    // returning errors as appropriate.)
    if (!model_->prepare()) {
        HLOG(ERROR) << "model_->prepare() failed.";
        return false;
    }

    const auto dump_model = [this](const char *msg, int min_verbosity) {
        if (options_.verbosity >= min_verbosity) {
            std::ostringstream os;
            os << msg << "\n";
            model_->dump(os);
            HLOG(INFO) << os.str();
        }
    };

    dump_model("Model after prepare():", 3);

    model_ = pad_for_ops(std::move(model_));
    if (!model_) {
        HLOG(ERROR) << "pad_for_ops() failed.";
        return false;
    }
    dump_model("Model after pad_for_ops():", 3);

    model_ = in_place(std::move(model_));
    dump_model("Model after in_place():", 3);

    model_ = fold_constants(std::move(model_));
    dump_model("Model after fold_constants():", 3);

    model_ = flatten_groups(std::move(model_));
    dump_model("Model after flatten_groups:", 3);

    model_ = fuse_pad_ops(std::move(model_));
    if (!model_) {
        HLOG(ERROR) << "fuse_pad_ops() failed.";
        return false;
    }
    dump_model("Model after fuse_pad_ops:", 3);

    model_ = remove_dead_ops(std::move(model_));
    dump_model("Model after remove_dead_ops:", 3);

#ifndef NDEBUG
    do_check_op_order(model_.get());
#endif
    assert(tensor_storage_arena_ == nullptr);
    tensor_storage_arena_ = allocate_tensors(model_.get(), options_);

#ifndef NDEBUG
    VerifyAllAllocated verify_all;
    model_->accept(&verify_all);
#endif

    dump_model("Model after all transformations:", 2);

    prepared_ = true;
    return true;
}

void Interpreter::execute() {
    if (!prepared_) {
        HLOG(ERROR) << "Must call prepare() before execute()";
        return;
    }
    model_->execute();
}

TensorPtr Interpreter::get_tensor(const std::string &name) {
    HCHECK(prepared_);

    class Finder : public OpVisitor {
        using OpVisitor::visit;

        bool find_tensor(const Op *op) {
            if (result) {
                return true;
            }
            for (int j = 0; j < op->input_count(); j++) {
                if (op->input(j)->name() == name_) {
                    result = op->input(j);
                    return true;
                }
            }
            for (int j = 0; j < op->output_count(); j++) {
                if (op->output(j)->name() == name_) {
                    result = op->output(j);
                    return true;
                }
            }
            return false;
        }

        void visit_leaf(const Op *op) override {
            if (find_tensor(op)) {
                return;
            }
        }

        void visit(const OpGroup *op) override {
            if (find_tensor(op)) {
                return;
            }
            OpVisitor::visit(op);
        }

        const std::string &name_;

    public:
        explicit Finder(const std::string &name)
            : name_(name) {
        }
        TensorPtr result = nullptr;
    };

    Finder finder(name);
    model_->accept(&finder);
    return finder.result;
}

std::vector<TensorPtr> Interpreter::inputs() {
    HCHECK(prepared_);
    std::vector<TensorPtr> result;
    for (int i = 0; i < model_->input_count(); i++) {
        result.push_back(model_->input(i));
    }

    return result;
}

std::vector<TensorPtr> Interpreter::outputs() {
    HCHECK(prepared_);
    std::vector<TensorPtr> result;
    for (int i = 0; i < model_->output_count(); i++) {
        result.push_back(model_->output(i));
    }

    return result;
}

}  // namespace hannk
