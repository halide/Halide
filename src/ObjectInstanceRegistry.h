#ifndef HALIDE_OBJECT_INSTANCE_REGISTRY_H
#define HALIDE_OBJECT_INSTANCE_REGISTRY_H

/** \file
 *
 * Provides a single global registry of Generators, GeneratorParams,
 * and Params indexed by this pointer. This is used for finding the
 * parameters inside of a Generator.
 */

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace Halide {
namespace Internal {

class ObjectInstanceRegistry {
public:
    enum Kind {
        Invalid,
        Generator,
        GeneratorParam,
        GeneratorInput,
        GeneratorOutput,
        FilterParam
    };

    /** Add an instance to the registry. The size may be 0 for Param Kinds,
     * but not for Generator. subject_ptr is the value actually associated
     * with this instance; it is usually (but not necessarily) the same
     * as this_ptr. Assert if this_ptr is already registered.
     *
     * If 'this' is directly heap allocated (not a member of a
     * heap-allocated object) and you want the introspection subsystem
     * to know about it and its members, set the introspection_helper
     * argument to a pointer to a global variable with the same true
     * type as 'this'. For example:
     *
     * MyObject *obj = new MyObject;
     * static MyObject *introspection_helper = nullptr;
     * register_instance(obj, sizeof(MyObject), kind, obj, &introspection_helper);
     *
     * I.e. introspection_helper should be a pointer to a pointer to
     * an object instance. The inner pointer can be null. The
     * introspection subsystem will then assume this new object is of
     * the matching type, which will help its members deduce their
     * names on construction.
     */
    static void register_instance(void *this_ptr, size_t size, Kind kind, void *subject_ptr,
                                  const void *introspection_helper);

    /** Remove an instance from the registry. Assert if not found.
     */
    static void unregister_instance(void *this_ptr);

    /** Returns the list of subject pointers for objects that have
     * been directly registered within the given range. If there is
     * another containing object inside the range, instances within
     * that object are skipped.
     */
    static std::vector<std::pair<void *, Kind>> instances_in_range(void *start, size_t size);

private:
    static ObjectInstanceRegistry &get_registry();

    struct InstanceInfo {
        void *subject_ptr = nullptr;  // May be different from the this_ptr in the key
        size_t size = 0;              // May be 0 for params
        Kind kind = Invalid;
        bool registered_for_introspection = false;

        InstanceInfo() = default;
        InstanceInfo(size_t size, Kind kind, void *subject_ptr, bool registered_for_introspection)
            : subject_ptr(subject_ptr), size(size), kind(kind), registered_for_introspection(registered_for_introspection) {
        }
    };

    std::mutex mutex;
    std::map<uintptr_t, InstanceInfo> instances;

    ObjectInstanceRegistry() = default;

public:
    ObjectInstanceRegistry(const ObjectInstanceRegistry &) = delete;
    ObjectInstanceRegistry &operator=(const ObjectInstanceRegistry &) = delete;
    ObjectInstanceRegistry(ObjectInstanceRegistry &&) = delete;
    ObjectInstanceRegistry &operator=(ObjectInstanceRegistry &&) = delete;
};

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_OBJECT_INSTANCE_REGISTRY_H
