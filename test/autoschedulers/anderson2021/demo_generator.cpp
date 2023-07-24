/*
Expected ASAN error:

env LD_LIBRARY_PATH=/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib /home/antony/Projects/ProxImaL/proximal/halide/build-clang/src/user-problem/solver-generator -o /home/antony/Projects/ProxImaL/proximal/halide/build-clang/src/user-problem -g debug_norm -e static_library,h target=host-cuda-cuda_capability_75 -p autoschedule_anderson2021 autoscheduler=Anderson2021 autoscheduler.parallelism=32
AddressSanitizer:DEADLYSIGNAL
=================================================================
==1236944==ERROR: AddressSanitizer: SEGV on unknown address 0x000000000070 (pc 0x7f00983507ce bp 0x000000000008 sp 0x7ffe56413000 T0)
==1236944==The signal is caused by a READ memory access.
==1236944==Hint: address points to the zero page.
    #0 0x7f00983507ce in Halide::Internal::Autoscheduler::LoopNest::compute_strides(Halide::Internal::Autoscheduler::LoadJacobian const&, int, Halide::Internal::Autoscheduler::FunctionDAG::Node const*, Halide::Internal::IntrusivePtr<Halide::Internal::Autoscheduler::BoundContents const> const&, Halide::Internal::Autoscheduler::ThreadInfo const&, bool) const (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libautoschedule_anderson2021.so+0x657ce)
    #1 0x7f0098372f6c in void Halide::Internal::Autoscheduler::LoopNest::compute_num_mem_accesses_per_block<Halide::Internal::Autoscheduler::GlobalMem>(Halide::Internal::Autoscheduler::LoadJacobian const&, Halide::Internal::Autoscheduler::FunctionDAG::Node const*, Halide::Internal::IntrusivePtr<Halide::Internal::Autoscheduler::BoundContents const> const&, Halide::Internal::Autoscheduler::ThreadInfo const&, int, double, Halide::Internal::Autoscheduler::MemInfo<Halide::Internal::Autoscheduler::MemTraits<Halide::Internal::Autoscheduler::GlobalMem>::MemInfoType>&, bool) const (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libautoschedule_anderson2021.so+0x87f6c)
    #2 0x7f0098373f9d in void Halide::Internal::Autoscheduler::LoopNest::compute_mem_load_features<Halide::Internal::Autoscheduler::GlobalMem>(Halide::Internal::Autoscheduler::LoadJacobian const&, int, Halide::Internal::Autoscheduler::FunctionDAG::Node const*, Halide::Internal::IntrusivePtr<Halide::Internal::Autoscheduler::BoundContents const> const&, bool, Halide::Internal::Autoscheduler::ThreadInfo const&, Halide::Internal::Autoscheduler::MemInfo<Halide::Internal::Autoscheduler::MemTraits<Halide::Internal::Autoscheduler::GlobalMem>::MemInfoType>&, double, bool) const (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libautoschedule_anderson2021.so+0x88f9d)
    #3 0x7f009835ca37 in Halide::Internal::Autoscheduler::LoopNest::compute_features(Halide::Internal::Autoscheduler::FunctionDAG const&, Halide::Internal::Autoscheduler::Anderson2021Params const&, Halide::Target const&, PerfectHashMap<Halide::Internal::Autoscheduler::FunctionDAG::Node::Stage, Halide::Internal::Autoscheduler::LoopNest::Sites, 4, PerfectHashMapAsserter> const&, long, long, Halide::Internal::Autoscheduler::LoopNest const*, Halide::Internal::Autoscheduler::LoopNest const*, Halide::Internal::Autoscheduler::LoopNest const&, Halide::Internal::Autoscheduler::GPULoopInfo, bool, PerfectHashMap<Halide::Internal::Autoscheduler::FunctionDAG::Node::Stage, long, 4, PerfectHashMapAsserter> const&, long*, long*, long*, PerfectHashMap<Halide::Internal::Autoscheduler::FunctionDAG::Node::Stage, Halide::Internal::ScheduleFeatures, 4, PerfectHashMapAsserter>*, Halide::Internal::Autoscheduler::Statistics&, bool) const (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libautoschedule_anderson2021.so+0x71a37)
    #4 0x7f009835b0ab in Halide::Internal::Autoscheduler::LoopNest::compute_features(Halide::Internal::Autoscheduler::FunctionDAG const&, Halide::Internal::Autoscheduler::Anderson2021Params const&, Halide::Target const&, PerfectHashMap<Halide::Internal::Autoscheduler::FunctionDAG::Node::Stage, Halide::Internal::Autoscheduler::LoopNest::Sites, 4, PerfectHashMapAsserter> const&, long, long, Halide::Internal::Autoscheduler::LoopNest const*, Halide::Internal::Autoscheduler::LoopNest const*, Halide::Internal::Autoscheduler::LoopNest const&, Halide::Internal::Autoscheduler::GPULoopInfo, bool, PerfectHashMap<Halide::Internal::Autoscheduler::FunctionDAG::Node::Stage, long, 4, PerfectHashMapAsserter> const&, long*, long*, long*, PerfectHashMap<Halide::Internal::Autoscheduler::FunctionDAG::Node::Stage, Halide::Internal::ScheduleFeatures, 4, PerfectHashMapAsserter>*, Halide::Internal::Autoscheduler::Statistics&, bool) const (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libautoschedule_anderson2021.so+0x700ab)
    #5 0x7f009835ac49 in Halide::Internal::Autoscheduler::LoopNest::compute_features(Halide::Internal::Autoscheduler::FunctionDAG const&, Halide::Internal::Autoscheduler::Anderson2021Params const&, Halide::Target const&, PerfectHashMap<Halide::Internal::Autoscheduler::FunctionDAG::Node::Stage, Halide::Internal::Autoscheduler::LoopNest::Sites, 4, PerfectHashMapAsserter> const&, long, long, Halide::Internal::Autoscheduler::LoopNest const*, Halide::Internal::Autoscheduler::LoopNest const*, Halide::Internal::Autoscheduler::LoopNest const&, Halide::Internal::Autoscheduler::GPULoopInfo, bool, PerfectHashMap<Halide::Internal::Autoscheduler::FunctionDAG::Node::Stage, long, 4, PerfectHashMapAsserter> const&, long*, long*, long*, PerfectHashMap<Halide::Internal::Autoscheduler::FunctionDAG::Node::Stage, Halide::Internal::ScheduleFeatures, 4, PerfectHashMapAsserter>*, Halide::Internal::Autoscheduler::Statistics&, bool) const (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libautoschedule_anderson2021.so+0x6fc49)
    #6 0x7f009838c1bd in Halide::Internal::Autoscheduler::State::compute_featurization(Halide::Internal::Autoscheduler::FunctionDAG const&, Halide::Internal::Autoscheduler::Anderson2021Params const&, Halide::Target const&, PerfectHashMap<Halide::Internal::Autoscheduler::FunctionDAG::Node::Stage, Halide::Internal::ScheduleFeatures, 4, PerfectHashMapAsserter>*, Halide::Internal::Autoscheduler::Statistics&, bool) const (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libautoschedule_anderson2021.so+0xa11bd)
    #7 0x7f009838cec0 in Halide::Internal::Autoscheduler::State::calculate_cost(Halide::Internal::Autoscheduler::FunctionDAG const&, Halide::Internal::Autoscheduler::Anderson2021Params const&, Halide::Target const&, Halide::CostModel*, Halide::Internal::Autoscheduler::Statistics&, bool) (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libautoschedule_anderson2021.so+0xa1ec0)
    #8 0x7f009837c076 in Halide::Internal::Autoscheduler::SearchSpace::add_child(Halide::Internal::IntrusivePtr<Halide::Internal::Autoscheduler::State> const&, Halide::Internal::IntrusivePtr<Halide::Internal::Autoscheduler::LoopNest const> const&, std::function<void (Halide::Internal::IntrusivePtr<Halide::Internal::Autoscheduler::State>&&)>&) const (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libautoschedule_anderson2021.so+0x91076)
    #9 0x7f0098381839 in Halide::Internal::Autoscheduler::SearchSpace::generate_children(Halide::Internal::IntrusivePtr<Halide::Internal::Autoscheduler::State> const&, std::function<void (Halide::Internal::IntrusivePtr<Halide::Internal::Autoscheduler::State>&&)>&, int, bool) (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libautoschedule_anderson2021.so+0x96839)
    #10 0x7f009830926e in Halide::Internal::Autoscheduler::AutoSchedule::optimal_schedule_pass(int, int, int, Halide::Internal::Autoscheduler::ProgressBar&, std::unordered_set<unsigned long, std::hash<unsigned long>, std::equal_to<unsigned long>, std::allocator<unsigned long> >&) (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libautoschedule_anderson2021.so+0x1e26e)
    #11 0x7f009830b144 in Halide::Internal::Autoscheduler::AutoSchedule::optimal_schedule(int) (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libautoschedule_anderson2021.so+0x20144)
    #12 0x7f009830c0d2 in Halide::Internal::Autoscheduler::generate_schedule(std::vector<Halide::Internal::Function, std::allocator<Halide::Internal::Function> > const&, Halide::Target const&, Halide::Internal::Autoscheduler::Anderson2021Params const&, Halide::AutoSchedulerResults*) (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libautoschedule_anderson2021.so+0x210d2)
    #13 0x7f0098317f4b in Halide::Internal::Autoscheduler::Anderson2021::operator()(Halide::Pipeline const&, Halide::Target const&, Halide::AutoschedulerParams const&, Halide::AutoSchedulerResults*) (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libautoschedule_anderson2021.so+0x2cf4b)
    #14 0x7f009cf22c82 in Halide::Pipeline::apply_autoscheduler(Halide::Target const&, Halide::AutoschedulerParams const&) const (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libHalide.so.16+0xd9fc82)
    #15 0x7f009ca56e7c in Halide::Internal::AbstractGenerator::build_module(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&) (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libHalide.so.16+0x8d3e7c)
    #16 0x7f009cd78eaa in std::_Function_handler<Halide::Module (std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, Halide::Target const&), Halide::Internal::execute_generator(Halide::Internal::ExecuteGeneratorArgs const&)::'lambda2'(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, Halide::Target const&)>::_M_invoke(std::_Any_data const&, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, Halide::Target const&) (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libHalide.so.16+0xbf5eaa)
    #17 0x7f009ceec309 in Halide::compile_multitarget(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, std::map<Halide::OutputFileType, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >, std::less<Halide::OutputFileType>, std::allocator<std::pair<Halide::OutputFileType const, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > > > > const&, std::vector<Halide::Target, std::allocator<Halide::Target> > const&, std::vector<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >, std::allocator<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > > > const&, std::function<Halide::Module (std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, Halide::Target const&)> const&, std::function<std::unique_ptr<Halide::Internal::CompilerLogger, std::default_delete<Halide::Internal::CompilerLogger> > (std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, Halide::Target const&)> const&) (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libHalide.so.16+0xd69309)
    #18 0x7f009cd81edf in Halide::Internal::execute_generator(Halide::Internal::ExecuteGeneratorArgs const&) (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libHalide.so.16+0xbfeedf)
    #19 0x7f009cd834ed in Halide::Internal::(anonymous namespace)::generate_filter_main_inner(int, char**, Halide::Internal::GeneratorFactoryProvider const&) (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libHalide.so.16+0xc004ed)
    #20 0x7f009cd8427f in Halide::Internal::generate_filter_main(int, char**, Halide::Internal::GeneratorFactoryProvider const&) (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libHalide.so.16+0xc0127f)
    #21 0x7f009cd842ca in Halide::Internal::generate_filter_main(int, char**) (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libHalide.so.16+0xc012ca)
    #22 0x7f009bc0c082 in __libc_start_main /build/glibc-SzIz7B/glibc-2.31/csu/../csu/libc-start.c:308:16
    #23 0x42346d in _start (/home/antony/Projects/ProxImaL/proximal/halide/build-clang/src/user-problem/solver-generator+0x42346d)

AddressSanitizer can not provide additional info.
SUMMARY: AddressSanitizer: SEGV (/home/antony/Projects/ProxImaL/proximal/halide/subprojects/Halide-16.0.0-x86-64-linux/lib/libautoschedule_anderson2021.so+0x657ce) in Halide::Internal::Autoscheduler::LoopNest::compute_strides(Halide::Internal::Autoscheduler::LoadJacobian const&, int, Halide::Internal::Autoscheduler::FunctionDAG::Node const*, Halide::Internal::IntrusivePtr<Halide::Internal::Autoscheduler::BoundContents const> const&, Halide::Internal::Autoscheduler::ThreadInfo const&, bool) const
==1236944==ABORTING
*/
#include <Halide.h>
using namespace Halide;

#include <array>

namespace {

/** A tuple of Halide stages having various n_dim0 != n_dim1, ... and so on. */
template<size_t N>
using FuncTuple = std::array<Func, N>;

/** Compute the L2 norm of a vector.
 *
 * If a tuple of matrices and/or tensors are given as inputs, vectorize and
 * concatentate them to form a giant 1D vector. Then, compute the L2 norm of the
 * single vector.
 * 
 * Note(Antony): I am open to a code refactoring to work-around the Segfault in Anderson2021 scheduler.
 */
template<size_t N>
inline Expr
norm(const FuncTuple<N> &v, const RDom &r) {
    Expr s = 0.0f;

    // Bug(Antony): Segfault here.
    for (const auto &_v : v) {
        const auto n_dims = _v.dimensions();
        assert((n_dims >= 1 && n_dims <= 4) && "Assuming 1D to 4D signals");

        switch (n_dims) {
        case 4:
            s += sum(_v(r.x, r.y, r.z, r.w) * _v(r.x, r.y, r.z, r.w));
            break;

        case 3:
            s += sum(_v(r.x, r.y, r.z) * _v(r.x, r.y, r.z));
            break;

        case 2:
            s += sum(_v(r.x, r.y) * _v(r.x, r.y));
            break;

        case 1:
            s += sum(_v(r.x) * _v(r.x));
            break;
        }
    }

    return sqrt(s);
}

class DebugAnderson2021Segfault : public Generator<DebugAnderson2021Segfault> {
    static constexpr auto W = 128;
    static constexpr auto H = W;

public:
    /** Multi-channel image gradient. */
    Input<Buffer<float, 4>> z0{"z0"};

    /** Multi-channel image. */
    Input<Buffer<float, 3>> z1{"z1"};

    Output<float> r{"r"};  //!< Primal residual
    void generate() {
        const RDom win{0, W, 0, H, 0, 1, 0, 2};

        const FuncTuple<2> z_list{z0, z1};
        r() = norm(z_list, win);
    }

    /** Inform Halide of the fixed input and output image sizes. */
    inline void setBounds() {
        z1.dim(0).set_bounds(0, W);
        z1.dim(1).set_bounds(0, H);
        z1.dim(2).set_bounds(0, 1);

        z0.dim(0).set_bounds(0, W);
        z0.dim(1).set_bounds(0, H);
        z0.dim(2).set_bounds(0, 1);
        z0.dim(3).set_bounds(0, 2);
    }

    void schedule() {
        setBounds();

        assert(using_autoscheduler() && "Fatal: test case not intended for manual scheduling");

        // Estimate the image sizes of the inputs.
        z1.set_estimates({{0, W}, {0, H}, {0, 1}});
        z0.set_estimates({{0, W}, {0, H}, {0, 1}, {0, 2}});
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(DebugAnderson2021Segfault, demo);