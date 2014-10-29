#include "ObjectInstanceRegistry.h"
#include "Error.h"

namespace Halide {
namespace Internal {

/* static */
ObjectInstanceRegistry &ObjectInstanceRegistry::get_registry() {
    static ObjectInstanceRegistry *registry = new ObjectInstanceRegistry;
    return *registry;
}

/* static */
void ObjectInstanceRegistry::register_instance(void *this_ptr, size_t size, Kind kind,
                                               void *subject_ptr) {
#if __cplusplus > 199711L || _MSC_VER >= 1800
    ObjectInstanceRegistry &registry = get_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    uintptr_t key = (uintptr_t)this_ptr;
    internal_assert(registry.instances.find(key) == registry.instances.end());
    registry.instances[key] = InstanceInfo(size, kind, subject_ptr);
#endif
}

/* static */
void ObjectInstanceRegistry::unregister_instance(void *this_ptr) {
#if __cplusplus > 199711L || _MSC_VER >= 1800
    ObjectInstanceRegistry &registry = get_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    uintptr_t key = (uintptr_t)this_ptr;
    size_t num_erased = registry.instances.erase(key);
    internal_assert(num_erased == 1) << "num_erased: " << num_erased;
#endif
}

/* static */
std::vector<void *> ObjectInstanceRegistry::instances_in_range(void *start, size_t size,
                                                               Kind kind) {
    std::vector<void *> results;

#if __cplusplus > 199711L || _MSC_VER >= 1800
    ObjectInstanceRegistry &registry = get_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);

    std::map<uintptr_t, InstanceInfo>::const_iterator it =
        registry.instances.lower_bound((uintptr_t)start);

    uintptr_t limit_ptr = ((uintptr_t)start) + size;
    while (it != registry.instances.end() && it->first < limit_ptr) {
        if (it->second.kind == kind) {
            results.push_back(it->second.subject_ptr);
        }

        if (it->first > (uintptr_t)start && it->second.size != 0) {
            // Skip over containers that we enclose
            it = registry.instances.lower_bound(it->first + it->second.size);
        } else {
            it++;
        }
    }
#endif

    return results;
}

}  // namespace Internal
}  // namespace Halide
