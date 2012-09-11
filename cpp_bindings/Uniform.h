#ifndef HALIDE_UNIFORM_H
#define HALIDE_UNIFORM_H

#include "Type.h"
#include "Util.h"
#include <assert.h>

namespace Halide {

    // Dynamically and statically typed uniforms

    class DynUniform {
    public:
        DynUniform(Type t) : contents(new Contents(t, uniqueName('u'))) {}
        DynUniform(Type t, const std::string &name) : contents(new Contents(t, sanitizeName(name))) {}

        Type type() const {return contents->type;}
        const std::string &name() const {return contents->name;}

        template<typename T>
        void set(T v) {
            assert(TypeOf<T>() == type());
            contents->val = 0; 
            T *ptr = (T *)(&(contents->val));
            *ptr = v;
        }

        bool operator==(const DynUniform &other) {
            return contents == other.contents;
        }

        void *data() const {
            return (&contents->val);
        }

    private:
        struct Contents {
            Contents(Type t, const std::string &n) : val(0), name(n), type(t) {}
            int64_t val;
            const std::string name;
            Type type;
        };
        shared_ptr<Contents> contents;
    };

    template<typename T>
    class Uniform {
    public:
        Uniform(const T &v = 0) : u(TypeOf<T>()) {
            u.set(v);
        }

        Uniform(const std::string &name, const T &v = 0) : u(TypeOf<T>(), name) {
            u.set(v);
        }

        void operator=(T v) {
            u.set(v);
        }
        
        operator DynUniform() const {
            return u;
        }

        Type type() const {return TypeOf<T>();}
        const std::string &name() const {return u.name();}
        void *data() const {return u.data();}

        //operator size_t() const { return (size_t)data(); }

    private:
        DynUniform u;
    };

}

#endif
