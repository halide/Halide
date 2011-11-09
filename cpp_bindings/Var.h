#ifndef FIMAGE_VAR_H
#define FIMAGE_VAR_H

#include "Util.h"

namespace FImage {

    // A loop variable
    class Var {
    public:
        Var() : _name(uniqueName('v')) {}
        Var(const std::string &name) : _name(name) {}
        const std::string &name() const {return _name;}
        
        bool operator==(const Var &other) const {
            return name() == other.name();
        }
        
    private:
        std::string _name;
    };

}

#endif
