#include "ObjectInstanceRegistry.h"
#include "Error.h"
#include "Introspection.h"

namespace Halide {
namespace Internal {

/* static */
ObjectInstanceRegistry &ObjectInstanceRegistry::get_registry() {
    static ObjectInstanceRegistry *registry = new ObjectInstanceRegistry;
    return *registry;
}

/* static */
void ObjectInstanceRegistry::register_instance(void *this_ptr, size_t size, Kind kind,
                                               void *subject_ptr, const void *introspection_helper) {
    ObjectInstanceRegistry &registry = get_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    uintptr_t key = (uintptr_t)this_ptr;
    internal_assert(registry.instances.find(key) == registry.instances.end());
    if (introspection_helper) {
        registry.instances[key] = InstanceInfo(size, kind, subject_ptr, true);
        Introspection::register_heap_object(this_ptr, size, introspection_helper);
    } else {
        registry.instances[key] = InstanceInfo(size, kind, subject_ptr, false);
    }
}

/* static */
void ObjectInstanceRegistry::unregister_instance(void *this_ptr) {
    ObjectInstanceRegistry &registry = get_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    uintptr_t key = (uintptr_t)this_ptr;
    std::map<uintptr_t, InstanceInfo>::iterator it = registry.instances.find(key);
    internal_assert(it != registry.instances.end());
    if (it->second.registered_for_introspection) {
        Introspection::deregister_heap_object(this_ptr, it->second.size);
    }
    registry.instances.erase(it);
}

/* static */
std::vector<std::pair<void *, ObjectInstanceRegistry::Kind>>
ObjectInstanceRegistry::instances_in_range(void *start, size_t size) {
    std::vector<std::pair<void *, ObjectInstanceRegistry::Kind>> results;

    ObjectInstanceRegistry &registry = get_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);

    std::map<uintptr_t, InstanceInfo>::const_iterator it =
        registry.instances.lower_bound((uintptr_t)start);

    uintptr_t limit_ptr = ((uintptr_t)start) + size;
    while (it != registry.instances.end() && it->first < limit_ptr) {
        results.emplace_back(it->second.subject_ptr, it->second.kind);

        if (it->first > (uintptr_t)start && it->second.size != 0) {
            // Skip over containers that we enclose
            it = registry.instances.lower_bound(it->first + it->second.size);
        } else {
            it++;
        }
    }

    return results;
}

}  // namespace Internal
}  // namespace Halide
