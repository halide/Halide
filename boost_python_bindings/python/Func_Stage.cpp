#include "Func_Stage.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
//#include "add_operators.h"

#include "../../src/Func.h"

#include <vector>
#include <string>

namespace h = Halide;
namespace p = boost::python;
using p::self;

void defineStage()
{
    using Halide::Stage;

    // only defined so that boost::python knows about these classes,
    // not (yet) meant to be created or manipulated by the user
    p::class_<Stage>("Stage", p::no_init)
            //    Stage(Internal::Schedule s, const std::string &n) :

            .def("dump_argument_list", &Stage::dump_argument_list, p::arg("self"),
                 "Return a string describing the current var list taking into "
                 "account all the splits, reorders, and tiles.")

            .def("name", &Stage::name, p::arg("self"),
                 p::return_value_policy<p::copy_const_reference>(),
                 "Return the name of this stage, e.g. \"f.update(2)\"")


            ///** Scheduling calls that control how the domain of this stage is
            // * traversed. See the documentation for Func for the meanings. */
            //// @{

            //Stage &split(VarOrRVar old, VarOrRVar outer, VarOrRVar inner, Expr factor);
            //Stage &fuse(VarOrRVar inner, VarOrRVar outer, VarOrRVar fused);
            //Stage &serial(VarOrRVar var);
            //Stage &parallel(VarOrRVar var);
            //Stage &vectorize(VarOrRVar var);
            //Stage &unroll(VarOrRVar var);
            //Stage &parallel(VarOrRVar var, Expr task_size);
            //Stage &vectorize(VarOrRVar var, int factor);
            //Stage &unroll(VarOrRVar var, int factor);
            //Stage &tile(VarOrRVar x, VarOrRVar y,
            //                            VarOrRVar xo, VarOrRVar yo,
            //                            VarOrRVar xi, VarOrRVar yi, Expr
            //                            xfactor, Expr yfactor);
            //Stage &tile(VarOrRVar x, VarOrRVar y,
            //                            VarOrRVar xi, VarOrRVar yi,
            //                            Expr xfactor, Expr yfactor);
            //Stage &reorder(const std::vector<VarOrRVar> &vars);

            //template <typename... Args>
            //NO_INLINE typename std::enable_if<Internal::all_are_convertible<VarOrRVar, Args...>::value, Stage &>::type
            //reorder(VarOrRVar x, VarOrRVar y, Args... args) {
            //    std::vector<VarOrRVar> collected_args;
            //    collected_args.push_back(x);
            //    collected_args.push_back(y);
            //    Internal::collect_args(collected_args, args...);
            //    return reorder(collected_args);
            //}

            //Stage &rename(VarOrRVar old_name, VarOrRVar new_name);
            //Stage specialize(Expr condition);

            //Stage &gpu_threads(VarOrRVar thread_x, DeviceAPI device_api = DeviceAPI::Default_GPU);
            //Stage &gpu_threads(VarOrRVar thread_x, VarOrRVar thread_y, DeviceAPI device_api = DeviceAPI::Default_GPU);
            //Stage &gpu_threads(VarOrRVar thread_x, VarOrRVar thread_y, VarOrRVar thread_z, DeviceAPI device_api = DeviceAPI::Default_GPU);
            //Stage &gpu_single_thread(DeviceAPI device_api = DeviceAPI::Default_GPU);

            //Stage &gpu_blocks(VarOrRVar block_x, DeviceAPI device_api = DeviceAPI::Default_GPU);
            //Stage &gpu_blocks(VarOrRVar block_x, VarOrRVar block_y, DeviceAPI device_api = DeviceAPI::Default_GPU);
            //Stage &gpu_blocks(VarOrRVar block_x, VarOrRVar block_y, VarOrRVar block_z, DeviceAPI device_api = DeviceAPI::Default_GPU);

            //Stage &gpu(VarOrRVar block_x, VarOrRVar thread_x, DeviceAPI device_api = DeviceAPI::Default_GPU);
            //Stage &gpu(VarOrRVar block_x, VarOrRVar block_y,
            //                           VarOrRVar thread_x, VarOrRVar thread_y,
            //                           DeviceAPI device_api = DeviceAPI::Default_GPU);
            //Stage &gpu(VarOrRVar block_x, VarOrRVar block_y, VarOrRVar block_z,
            //                           VarOrRVar thread_x, VarOrRVar thread_y, VarOrRVar thread_z,
            //                           DeviceAPI device_api = DeviceAPI::Default_GPU);
            //Stage &gpu_tile(VarOrRVar x, Expr x_size, DeviceAPI device_api = DeviceAPI::Default_GPU);
            //Stage &gpu_tile(VarOrRVar x, VarOrRVar y, Expr x_size, Expr y_size,
            //                                DeviceAPI device_api = DeviceAPI::Default_GPU);
            //Stage &gpu_tile(VarOrRVar x, VarOrRVar y, VarOrRVar z,
            //                                Expr x_size, Expr y_size, Expr z_size, DeviceAPI device_api = DeviceAPI::Default_GPU);


            .def("allow_race_conditions", &Stage::allow_race_conditions, p::arg("self"),
                 p::return_internal_reference<1>())
            ;

    return;
}
