#include "ScheduleParam.h"
#include "ObjectInstanceRegistry.h"

namespace Halide {
namespace Internal {

ScheduleParamBase::ScheduleParamBase(const Type &t, const std::string &name, bool is_explicit_name)
    : sp_name(name),
      type(t),
      scalar_parameter(t, /*is_buffer*/ false, 0, is_explicit_name ? name + ".schedule_param_param" : "",
          is_explicit_name, /*register_instance*/ false, /*is_bound_before_lowering*/ true),
      scalar_expr(Variable::make(t, scalar_parameter.name() + ".schedule_param_var", scalar_parameter)),
      loop_level() {
    ObjectInstanceRegistry::register_instance(this, 0, ObjectInstanceRegistry::ScheduleParam, this, nullptr);
}

// Just the default copy ctor, except we must call register_instance to avoid unpleasantries later
ScheduleParamBase::ScheduleParamBase(const ScheduleParamBase &that)
    : sp_name(that.sp_name),
      type(that.type),
      scalar_parameter(that.scalar_parameter),
      scalar_expr(that.scalar_expr),
      loop_level(that.loop_level) {
    ObjectInstanceRegistry::register_instance(this, 0, ObjectInstanceRegistry::ScheduleParam, this, nullptr);
}

ScheduleParamBase &ScheduleParamBase::operator=(const ScheduleParamBase &that) {
    if (this != &that) {
        // You can only assign SP's of the same type.
        internal_assert(type == that.type);
        // You can only assign SP's of the same name.
        internal_assert(sp_name == that.sp_name);
        sp_name = that.sp_name;
        type = that.type;
        scalar_parameter = that.scalar_parameter;
        scalar_expr = that.scalar_expr;
        loop_level = that.loop_level;
    }
    return *this;
}

ScheduleParamBase::~ScheduleParamBase() {
    ObjectInstanceRegistry::unregister_instance(this);
}

}
}
