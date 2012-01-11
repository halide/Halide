#ifndef FIMAGE_TYPE_H
#define FIMAGE_TYPE_H

#include "MLVal.h"
#include <sstream>

namespace FImage {
    // Possible types for image data
    class Type {
      public:
        MLVal mlval;
        unsigned char bits;
        enum {FLOAT = 0, INT = 1, UINT = 2} code;
        bool operator==(const Type &other) const {
            return bits == other.bits && code == other.code;
        }
        bool operator!=(const Type &other) const {
            return !(*this == other);
        }
        std::string str() const {
            std::string codes[] = {"float", "int", "uint"};
            std::ostringstream ss;
            ss << ((code < 3 && code >= 0) ? codes[code] : "malformed_type_");
            ss << (int)(bits);
            return ss.str();
        }
    };

    Type Float(unsigned char bits);
    Type Int(unsigned char bits);
    Type UInt(unsigned char bits);

    template<typename T> Type TypeOf();
    template<> Type TypeOf<float>();
    template<> Type TypeOf<double>();
    template<> Type TypeOf<unsigned char>();
    template<> Type TypeOf<unsigned short>();
    template<> Type TypeOf<unsigned int>();
    template<> Type TypeOf<bool>();
    template<> Type TypeOf<char>();
    template<> Type TypeOf<short>();
    template<> Type TypeOf<int>();
    template<> Type TypeOf<signed char>();
}


#endif
