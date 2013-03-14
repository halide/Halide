#include "Util.h"
#include <sstream>
// In C++ 11: #include <unordered_set>
#include <set>

namespace Halide { 
namespace Internal {

using std::string;
using std::ostringstream;

string unique_name(char prefix) {
    // arrays with static storage duration should be initialized to zero automatically
    static int instances[256]; 
    ostringstream str;
    str << prefix << instances[(unsigned char)prefix]++;
    return str.str();
}

bool starts_with(const string &str, const string &prefix) {
    if (str.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); i++) {
        if (str[i] != prefix[i]) return false;
    }
    return true;
}

bool ends_with(const string &str, const string &suffix) {
    if (str.size() < suffix.size()) return false;
    size_t off = str.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); i++) {
        if (str[off+i] != suffix[i]) return false;
    }
    return true;
}

// [LH] Unique_name for programmer specified names.
// C++ 11: static std::unordered_set<std::string> known_names;
static std::set<std::string> known_names;
    
std::string unique_name(const std::string &name)
{
    std::string thename;
    
    // If the programmer specified a single character name then use the
    // pre-existing Halide unique name generator.
    if (name.length() == 1)
        return unique_name(name[0]);
    
    // An empty string really does not make sense, but use 'z' as prefix.
    if (name.length() == 0)
        return unique_name('z');
    
    // Use the programmer-specified name but append a number to make it unique.
    for (int i = 1; i < 1000000; i++)
    {
        if (i > 1)
        {
            std::ostringstream oss;
            oss << name << i;
            thename = oss.str();
        }
        else
        {
            // The very first unique name is the original function name itself.
            thename = name;
        }
        if (known_names.count(thename) <= 0)
        {
            // This generated name is not known already, so mark it used and return it.
            known_names.insert(thename);
            break;
        }
    }
    
    return thename;
}

}
}
