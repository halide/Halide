#include "Introspection.h"

#ifdef WITH_INTROSPECTION

#include "Debug.h"
#include "LLVM_Headers.h"
#include "Error.h"

#include <string>
#include <iostream>
#include <sstream>
#include <stdio.h>

// defines backtrace, which gets the call stack as instruction pointers
#include <execinfo.h>

using std::vector;
using std::pair;
using std::map;

namespace Halide {
namespace Internal {
namespace Introspection {

// All of this only works with DWARF debug info on linux and OS X. For
// other platforms, WITH_INTROSPECTION should be off.
#ifdef __APPLE__
extern "C" void _NSGetExecutablePath(char *, int32_t *);
void get_program_name(char *name, int32_t size) {
    _NSGetExecutablePath(name, &size);
}
#else
// glibc defines the binary name for us
extern "C" char *program_invocation_name;
void get_program_name(char *name, int32_t size) {
    strncpy(name, program_invocation_name, size);
}
#endif

class DebugSections {

    bool calibrated;

    struct FieldFormat {
        uint64_t name, form;
        FieldFormat() : name(0), form(0) {}
        FieldFormat(uint64_t n, uint64_t f) : name(n), form(f) {}
    };

    struct EntryFormat {
        uint64_t code, tag;
        bool has_children;
        EntryFormat() : code(0), tag(0), has_children(false) {}
        vector<FieldFormat> fields;
    };
    vector<EntryFormat> entry_formats;

    struct LiveRange {
        uint64_t pc_begin, pc_end;
    };

    struct TypeInfo;

    struct GlobalVariable {
        std::string name;
        TypeInfo *type;
        uint64_t type_def_loc;
        uint64_t def_loc, spec_loc;
        uint64_t addr;
        GlobalVariable() : name(""),
                           type(NULL),
                           type_def_loc(0),
                           def_loc(0),
                           spec_loc(0),
                           addr(0) {}
        bool operator<(const GlobalVariable &other) const {
            return addr < other.addr;
        }
    };
    vector<GlobalVariable> global_variables;

    struct HeapObject {
        uint64_t addr;
        TypeInfo *type;
        struct Member {
            uint64_t addr;
            std::string name;
            TypeInfo *type;
            bool operator<(const Member &other) const {
                return addr < other.addr;
            }
        };
        vector<Member> members;
    };
    map<uint64_t, HeapObject> heap_objects;

    struct LocalVariable {
        std::string name;
        TypeInfo *type;
        int stack_offset;
        uint64_t type_def_loc;
        uint64_t def_loc, origin_loc;
        // Some local vars are only alive for certain address ranges
        // (e.g. those inside a lexical block). If the ranges vector
        // is empty, the variables are alive for the entire containing
        // function.
        vector<LiveRange> live_ranges;
        LocalVariable() : name(""),
                          type(NULL),
                          stack_offset(0),
                          type_def_loc(0),
                          def_loc(0),
                          origin_loc(0) {}
    };

    struct FunctionInfo {
        std::string name;
        uint64_t pc_begin, pc_end;
        vector<LocalVariable> variables;
        uint64_t def_loc, spec_loc;
        // The stack variable offsets are w.r.t either:
        // gcc: the top of the stack frame (one below the return address to the caller)
        // clang with frame pointers: the bottom of the stack frame (one above the return address to this function)
        // clang without frame pointers: the top of the stack frame (...TODO...)
        enum {Unknown = 0, GCC, ClangFP, ClangNoFP} frame_base;
        FunctionInfo() : name(""), pc_begin(0), pc_end(0), def_loc(0), spec_loc(0) {}

        bool operator<(const FunctionInfo &other) const {
            return pc_begin < other.pc_begin;
        }
    };
    vector<FunctionInfo> functions;

    vector<std::string> source_files;
    struct LineInfo {
        uint64_t pc;
        uint32_t line;
        uint32_t file; // Index into source_files
        bool operator<(const LineInfo &other) const {
            return pc < other.pc;
        }
    };
    vector<LineInfo> source_lines;

    struct TypeInfo {
        std::string name;
        uint64_t size;
        uint64_t def_loc;
        vector<LocalVariable> members;

        // TypeInfo can also be used to represent a pointer to
        // another type, in which case there's a single member, which
        // represents the value pointed to (its name is empty and its
        // stack_offset is meaningless).
        enum {Primitive, Class, Struct, Pointer, Typedef, Const, Reference, Array} type;

        TypeInfo() : size(0), def_loc(0), type(Primitive) {}
    };
    vector<TypeInfo> types;

public:

    bool working;

    DebugSections(std::string binary) : calibrated(false), working(false) {
        #ifdef __APPLE__
        size_t last_slash = binary.rfind('/');
        if (last_slash == std::string::npos ||
            last_slash >= binary.size() - 1) {
            last_slash = 0;
        } else {
            last_slash++;
        }
        std::string file_only = binary.substr(last_slash, binary.size() - last_slash);
        binary += ".dSYM/Contents/Resources/DWARF/" + file_only;
        #endif

        debug(2) << "Loading " << binary << "\n";

        load_and_parse_object_file(binary);
    }

    int count_trailing_zeros(int64_t x) {
        for (int i = 0; i < 64; i++) {
            if (x & (1 << i)) {
                return i;
            }
        }
        return 64;
    }

    void calibrate_pc_offset(void (*fn)()) {
        // Calibrate for the offset between the instruction pointers
        // in the debug info and the instruction pointers in the
        // actual file.
        bool found = false;
        uint64_t pc_real = (uint64_t)fn;
        int64_t pc_adjust = 0;
        for (size_t i = 0; i < functions.size(); i++) {
            if (functions[i].name == "HalideIntrospectionCanary::offset_marker" &&
                functions[i].pc_begin) {

                uint64_t pc_debug = functions[i].pc_begin;

                if (calibrated) {
                    // If we're already calibrated, we should find a function with a matching pc
                    if (pc_debug == pc_real) {
                        return;
                    }
                } else {
                    int64_t pc_adj = pc_real - pc_debug;

                    // Offset must be a multiple of 4096
                    if (pc_adj & (4095)) {
                        continue;
                    }

                    // If we find multiple matches, pick the one with more trailing zeros
                    if (!found ||
                        count_trailing_zeros(pc_adj) > count_trailing_zeros(pc_adjust)) {
                        pc_adjust = pc_adj;
                        found = true;
                    }
                }
            }
        }

        if (!found) {
            if (!calibrated) {
                debug(1) << "Failed to find HalideIntrospectionCanary::offset_marker\n";
            } else {
                debug(1) << "Failed to find HalideIntrospectionCanary::offset_marker at the expected location\n";
            }
            working = false;
            return;
        }

        debug(4) << "Program counter adjustment between debug info and actual code: " << pc_adjust << "\n";

        for (size_t i = 0; i < functions.size(); i++) {
            FunctionInfo &f = functions[i];
            f.pc_begin += pc_adjust;
            f.pc_end += pc_adjust;
            for (size_t j = 0; j < f.variables.size(); j++) {
                LocalVariable &v = f.variables[j];
                for (size_t k = 0; k < v.live_ranges.size(); k++) {
                    v.live_ranges[k].pc_begin += pc_adjust;
                    v.live_ranges[k].pc_end += pc_adjust;
                }
            }
        }

        for (size_t i = 0; i < source_lines.size(); i++) {
            source_lines[i].pc += pc_adjust;
        }

        for (size_t i = 0; i < global_variables.size(); i++) {
            global_variables[i].addr += pc_adjust;
        }

        calibrated = true;
    }

    bool type_name_match(const std::string &actual_type, const std::string &query_type) {
        if (query_type.empty()) {
            return true;
        }
        if (query_type == actual_type) {
            return true;
        }
        if (query_type[query_type.size()-1] == '?' &&
            starts_with(actual_type, query_type.substr(0, query_type.size()-1))) {
            return true;
        }

        return false;
    }

    int find_global_variable(const void *global_pointer) {
        if (global_variables.empty()) {
            debug(4) << "Considering possible global at " << global_pointer << " but global_variables is empty\n";
            return -1;
        }
        debug(4) << "Considering possible global at " << global_pointer << "\n";

        debug(4) << "Known globals range from " << std::hex << global_variables.front().addr << " to " << global_variables.back().addr << std::dec << "\n";
        uint64_t address = (uint64_t)(global_pointer);
        size_t hi = global_variables.size();
        size_t lo = 0;
        while (hi > lo + 1) {
            size_t mid = (hi + lo)/2;
            uint64_t addr_mid = global_variables[mid].addr;
            if (address < addr_mid) {
                hi = mid;
            } else {
                lo = mid;
            }
        }

        if (lo >= global_variables.size()) {
            return -1;
        }

        // There may be multiple matching addresses. Walk backwards to find the first one.
        size_t idx = lo;
        while (idx > 0 && global_variables[idx-1].addr == global_variables[lo].addr) {
            idx--;
        }

        // Check the address is indeed inside the object found
        uint64_t end_ptr = global_variables[idx].addr;
        TypeInfo *t = global_variables[idx].type;
        uint64_t size = t->size;
        while (t->type == TypeInfo::Array) {
            t = t->members[0].type;
            size *= t->size;
        }
        end_ptr += size;
        if (address < global_variables[idx].addr ||
            address >= end_ptr) {
            return -1;
        }

        return (int)idx;
    }

    // Get the debug name of a global var from a pointer to it
    std::string get_global_variable_name(const void *global_pointer, const std::string &type_name = "") {
        // Find the index of the first global variable with this address
        int idx = find_global_variable(global_pointer);

        if (idx < 0) {
            // No matching global variable found.
            return "";
        }

        uint64_t address = (uint64_t)global_pointer;

        // Now test all of them
        for (; (size_t)idx < global_variables.size() && global_variables[idx].addr <= address; idx++) {

            GlobalVariable &v = global_variables[idx];
            TypeInfo *elem_type = NULL;
            if (v.type && v.type->type == TypeInfo::Array && v.type->size) {
                elem_type = v.type->members[0].type;
            }

            debug(4) << "Closest global is " << v.name << " at " << std::hex << v.addr << std::dec;
            if (v.type) {
                debug(4) << " with type " << v.type->name << "\n";
            } else {
                debug(4) << "\n";
            }

            if (v.addr == address &&
                (type_name.empty() ||
                 (v.type && type_name_match(v.type->name, type_name)))) {
                return v.name;
            } else if (elem_type && // Check if it's an array element
                       (type_name.empty() ||
                        (elem_type && type_name_match(elem_type->name, type_name)))) {
                int64_t array_size_bytes = v.type->size * elem_type->size;
                int64_t pos_bytes = address - v.addr;
                if (pos_bytes >= 0 &&
                    pos_bytes < array_size_bytes &&
                    pos_bytes % elem_type->size == 0) {
                    std::ostringstream oss;
                    oss << v.name << '[' << (pos_bytes / elem_type->size) << ']';
                    debug(4) << "Successful match to array element\n";
                    return oss.str();
                }
            }
        }

        // No match
        return "";
    }

    void register_heap_object(const void *obj, size_t size, const void *helper) {
        // helper should be a pointer to a global
        int idx = find_global_variable(helper);
        if (idx == -1) return;
        const GlobalVariable &ptr = global_variables[idx];
        debug(4) << "helper object is " << ptr.name << " at " << std::hex << ptr.addr << std::dec;
        if (ptr.type) {
            debug(4) << " with type " << ptr.type->name << "\n";
        } else {
            debug(4) << " with unknown type!\n";
            return;
        }

        internal_assert(ptr.type->type == TypeInfo::Pointer)
            << "The type of the helper object was supposed to be a pointer\n";
        internal_assert(ptr.type->members.size() == 1);
        TypeInfo *object_type = ptr.type->members[0].type;
        internal_assert(object_type);

        debug(4) << "The object has type: " << object_type->name << "\n";

        internal_assert(size == object_type->size);

        HeapObject heap_object;
        heap_object.type = object_type;
        heap_object.addr = (uint64_t)obj;

        // Recursively enumerate the members.
        for (size_t i = 0; i < object_type->members.size(); i++) {
            const LocalVariable &member_spec = object_type->members[i];
            HeapObject::Member member;
            member.name = member_spec.name;
            member.type = member_spec.type;
            member.addr = heap_object.addr + member_spec.stack_offset;
            if (member.type) {
                heap_object.members.push_back(member);
                debug(4) << member.name << " - " << (int)(member.type->type) << "\n";
            }
        }

        // Note that this loop pushes elements onto the vector it's
        // iterating over as it goes - that's what makes the
        // enumeration recursive.
        for (size_t i = 0; i < heap_object.members.size(); i++) {
            HeapObject::Member parent = heap_object.members[i];

            // Stop at references or pointers. We could register them
            // recursively (and basically write a garbage collector
            // object tracker), but that's beyond the scope of what
            // we're trying to do here. Besides, predicting the
            // addresses of their children-of-children might follow a
            // dangling pointer.
            if (parent.type->type == TypeInfo::Pointer ||
                parent.type->type == TypeInfo::Reference) continue;

            for (size_t j = 0; j < parent.type->members.size(); j++) {
                const LocalVariable &member_spec = parent.type->members[j];
                TypeInfo *member_type = member_spec.type;

                HeapObject::Member child;
                child.type = member_type;

                if (parent.type->type == TypeInfo::Typedef ||
                    parent.type->type == TypeInfo::Const) {
                    // We're just following a type modifier. It's still the same member.
                    child.name = parent.name;
                } else if (parent.type->type == TypeInfo::Array) {
                    child.name = ""; // the '[index]' gets added in the query routine.
                } else {
                    child.name = member_spec.name;
                }

                child.addr = parent.addr + member_spec.stack_offset;

                if (child.type) {
                    debug(4) << child.name << " - " << (int)(child.type->type) << "\n";
                    heap_object.members.push_back(child);
                }
            }
        }

        // Sort by member address, but use stable stort so that parents stay before children.
        std::stable_sort(heap_object.members.begin(), heap_object.members.end());

        debug(4) << "Children of heap object of type " << object_type->name << " at " << obj << ":\n";
        for (size_t i = 0; i < heap_object.members.size(); i++) {
            const HeapObject::Member &mem = heap_object.members[i];
            debug(4) << std::hex << mem.addr << std::dec << ": " << mem.type->name << " " << mem.name << "\n";
        }

        heap_objects[heap_object.addr] = heap_object;
    }

    void deregister_heap_object(const void *obj, size_t size) {
        heap_objects.erase((uint64_t)obj);
    }

    // Get the debug name of a member of a heap variable from a pointer to it
    std::string get_heap_member_name(const void *ptr, const std::string &type_name = "") {
        debug(4) << "Getting heap member name of " << ptr << "\n";

        if (heap_objects.empty()) {
            debug(4) << "No registered heap objects\n";
            return "";
        }

        uint64_t addr = (uint64_t)ptr;
        std::map<uint64_t, HeapObject>::iterator it = heap_objects.upper_bound(addr);

        if (it == heap_objects.begin()) {
            debug(4) << "No heap objects less than this address\n";
            return "";
        }

        // 'it' is the first element strictly greater than addr, so go
        // back one to get less-than-or-equal-to.
        it--;

        const HeapObject &obj = it->second;
        uint64_t object_start = it->first;
        uint64_t object_end = object_start + obj.type->size;
        if (addr < object_start || addr >= object_end) {
            debug(4) << "Not contained in any heap object\n";
            return "";
        }


        std::ostringstream name;

        // Look in the members for the appropriate offset.
        for (size_t i = 0; i < obj.members.size(); i++) {
            TypeInfo *t = obj.members[i].type;

            if (!t) continue;

            debug(4) << "Comparing to member " << obj.members[i].name
                     << " at address " << std::hex << obj.members[i].addr << std::dec
                     << " with type " << t->name
                     << " and type type " << (int)t->type << "\n";


            if (obj.members[i].addr == addr &&
                type_name_match(t->name, type_name)) {
                name << obj.members[i].name;
                return name.str();
            }

            // For arrays, we only unpacked the first element.
            if (t->type == TypeInfo::Array) {
                TypeInfo *elem_type = t->members[0].type;
                uint64_t array_start_addr = obj.members[i].addr;
                uint64_t array_end_addr = array_start_addr + t->size * elem_type->size;
                debug(4) << "Array runs from " << std::hex << array_start_addr << " to " << array_end_addr << "\n";
                if (elem_type && addr >= array_start_addr && addr < array_end_addr) {
                    // Adjust the query address backwards to lie
                    // within the first array element and remember the
                    // array index to correct the name later.
                    uint64_t containing_elem = (addr - array_start_addr) / elem_type->size;
                    addr -= containing_elem * elem_type->size;
                    debug(4) << "Query belongs to this array. Adjusting query address backwards to "
                             << std::hex << addr << std::dec << "\n";
                    name << obj.members[i].name << '[' << containing_elem << ']';
                }
            } else if (t->type == TypeInfo::Struct ||
                       t->type == TypeInfo::Class ||
                       t->type == TypeInfo::Primitive) {
                // If I'm not this member, but am contained within it, incorporate its name.
                uint64_t struct_start_addr = obj.members[i].addr;
                uint64_t struct_end_addr = struct_start_addr + t->size;
                debug(4) << "Struct runs from " << std::hex << struct_start_addr << " to " << struct_end_addr << "\n";
                if (addr >= struct_start_addr && addr < struct_end_addr) {
                    name << obj.members[i].name << '.';
                }
            }
        }

        debug(4) << "Didn't seem to be any of the members of this heap object\n";
        return "";
    }

    // Get the debug name of a stack variable from a pointer to it
    std::string get_stack_variable_name(const void *stack_pointer, const std::string &type_name = "") {

        // Check it's a plausible stack pointer
        int marker = 0;
        uint64_t marker_addr = (uint64_t)&marker;
        uint64_t top_of_stack;
        if (marker_addr >> 63) {
            top_of_stack = (uint64_t)(-1);
        } else {
            // Conservatively assume top of stack is first multiple of
            // 1GB larger than the marker (seriously, who allocates
            // 1GB of stack space?).
            top_of_stack = ((marker_addr >> 30) + 1) << 30;
        }

        if ((uint64_t)stack_pointer > top_of_stack ||
            (uint64_t)stack_pointer < marker_addr) {
            return "";
        }

        struct frame_info {
            frame_info *frame_pointer;
            void *return_address;
        };

        frame_info *fp = (frame_info *)__builtin_frame_address(0);
        frame_info *next_fp = NULL;

        // Walk up the stack until we pass the pointer.
        debug(4) << "Walking up the stack\n";
        while (fp < stack_pointer) {
            debug(4) << "frame pointer: " << (void *)(fp->frame_pointer)
                     << " return address: " << fp->return_address << "\n";
            next_fp = fp;
            if (fp->frame_pointer < fp) {
                // If we ever jump downwards, something is
                // wrong. Maybe this was a heap pointer.
                debug(4) << "Bailing out because fp decreased\n";
                return "";
            }
            fp = fp->frame_pointer;
            if (fp < (void *)&marker) {
                // If we're still below the marker after one hop,
                // something is wrong. Maybe this was a heap pointer.
                debug(4) << "Bailing out because we're below the marker\n";
                return "";
            }
        }

        if (!next_fp) {
            // If we didn't manage to walk up one frame, something is
            // wrong. Maybe this was a heap pointer.
            debug(4) << "Bailing out because we didn't even walk up one frame\n";
            return "";
        }

        // It's a stack variable in the function containing address
        // next_fp->return_address

        // Get the program counter at the position of the call
        uint64_t pc = (uint64_t)(next_fp->return_address) - 5; // -5 for the callq instruction

        FunctionInfo *func = find_containing_function(next_fp->return_address);

        if (!func) {
            debug(4) << "Bailing out because we couldn't find the containing function\n";
            return "";
        }

        // Now what is its offset in that function's frame? The return
        // address is always at the top of the frame.
        int offset_above = (int)((int64_t)(stack_pointer) - (int64_t)(fp));
        int offset_below = (int)((int64_t)(stack_pointer) - (int64_t)(next_fp));

        const int addr_size = sizeof(void *);

        int offset;
        if (func->frame_base == FunctionInfo::GCC) {
            offset = offset_above - 2*addr_size;
        } else if (func->frame_base == FunctionInfo::ClangFP) {
            offset = offset_above;
        } else if (func->frame_base == FunctionInfo::ClangNoFP) {
            offset = offset_below - 2*addr_size;
        } else {
            debug(4) << "Bailing out because containing function used an unknown mechanism for specifying stack offsets\n";
            return "";
        }

        debug(4) << "Searching for var at offset " << offset << "\n";

        for (size_t j = 0; j < func->variables.size(); j++) {
            const LocalVariable &var = func->variables[j];
            debug(4) << "Var " << var.name << " is at offset " << var.stack_offset << "\n";

            // Reject it if we're not in its live ranges
            if (var.live_ranges.size()) {
                bool in_live_range = false;
                for (size_t i = 0; i < var.live_ranges.size(); i++) {
                    if (pc >= var.live_ranges[i].pc_begin &&
                        pc < var.live_ranges[i].pc_end) {
                        in_live_range = true;
                        break;
                    }
                }
                if (!in_live_range) {
                    debug(4) << "Skipping var because we're not in any of its live ranges\n";
                    continue;
                }
            }

            TypeInfo *type = var.type;
            TypeInfo *elem_type = NULL;
            if (type && type->type == TypeInfo::Array && type->size) {
                elem_type = type->members[0].type;
            }

            if (offset == var.stack_offset && var.type) {
                debug(4) << "Considering match: " << var.type->name << ", " << var.name << "\n";
            }

            if (offset == var.stack_offset &&
                (type_name.empty() ||
                 (type && type_name_match(type->name, type_name)))) {
                debug(4) << "Successful match to scalar var\n";
                return var.name;
            } else if (elem_type && // Check if it's an array element
                       (type_name.empty() ||
                        (elem_type && // Check the type matches
                         type_name_match(elem_type->name, type_name)))) {
                int64_t array_size_bytes = type->size * elem_type->size;
                int64_t pos_bytes = offset - var.stack_offset;
                if (pos_bytes >= 0 &&
                    pos_bytes < array_size_bytes &&
                    pos_bytes % elem_type->size == 0) {
                    std::ostringstream oss;
                    oss << var.name << '[' << (pos_bytes / elem_type->size) << ']';
                    debug(4) << "Successful match to array element\n";
                    return oss.str();
                }
            }
        }

        debug(4) << "Failed to find variable at the matching offset with the given type\n";
        return "";
    }


    // Look up n stack frames and get the source location as filename:line
    std::string get_source_location() {

        debug(4) << "Finding source location\n";

        if (!source_lines.size()) {
            debug(4) << "Bailing out because we have no source lines\n";
            return "";
        }

        const int max_stack_frames = 256;

        // Get the backtrace
        vector<void *> trace(max_stack_frames);
        int trace_size = backtrace(&trace[0], (int)(trace.size()));

        for (int frame = 2; frame < trace_size; frame++) {
            uint64_t address = (uint64_t)trace[frame];

            debug(4) << "Considering address " << ((void *)address) << "\n";

            const uint8_t *inst_ptr = (const uint8_t *)address;
            if (inst_ptr[-5] == 0xe8) {
                // The actual address of the call is probably 5 bytes
                // earlier (using callq with an immediate address)
                address -= 5;
            } else if (inst_ptr[-2] == 0xff) {
                // Or maybe it's 2 bytes earlier (using callq with a
                // register address)
                address -= 2;
            } else {
                debug(4) << "Skipping function because there's no callq before " << (const void *)(inst_ptr) << "\n";
                continue;
            }

            // Binary search into functions
            FunctionInfo *f = find_containing_function((void *)address);

            // If no debug info for this function, we must still be
            // inside libHalide. Continue searching upwards.
            if (!f) {
                debug(4) << "Skipping function because we have no debug info for it\n";
                continue;
            }

            debug(4) << "Containing function is " << f->name << "\n";

            // If we're still in the Halide namespace, continue searching
            if (f->name.size() > 8 &&
                f->name.substr(0, 8) == "Halide::") {
                debug(4) << "Skipping function because it's in the Halide namespace\n";
                continue;
            }

            // Binary search into source_lines
            size_t hi = source_lines.size();
            size_t lo = 0;
            while (hi > lo + 1) {
                size_t mid = (hi + lo)/2;
                uint64_t pc_mid = source_lines[mid].pc;
                if (address < pc_mid) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }

            const std::string &file = source_files[source_lines[lo].file];
            int line = source_lines[lo].line;

            std::ostringstream oss;
            oss << file << ":" << line;

            debug(4) << "Source location is " << oss.str() << "\n";

            return oss.str();
        }

        debug(4) << "Bailing out because we reached the end of the backtrace\n";
        return "";
    }

    void dump() {
        // Dump all the types
        for (size_t i = 0; i < types.size(); i++) {
            printf("Class %s of size %llu @ %llx: \n",
                   types[i].name.c_str(),
                   (unsigned long long)(types[i].size),
                   (unsigned long long)(types[i].def_loc));
            for (size_t j = 0; j < types[i].members.size(); j++) {
                TypeInfo *c = types[i].members[j].type;
                const char *type_name = "(unknown)";
                if (c) {
                    type_name = c->name.c_str();
                }
                printf("  Member %s at %d of type %s @ %llx\n",
                       types[i].members[j].name.c_str(),
                       types[i].members[j].stack_offset,
                       type_name,
                       (long long unsigned)types[i].members[j].type_def_loc);
            }
        }

        // Dump all the functions and their local variables
        for (size_t i = 0; i < functions.size(); i++) {
            const FunctionInfo &f = functions[i];
            printf("Function %s at %llx - %llx (frame_base %d): \n",
                   f.name.c_str(),
                   (unsigned long long)(f.pc_begin),
                   (unsigned long long)(f.pc_end),
                   (int)f.frame_base);
            for (size_t j = 0; j < f.variables.size(); j++) {
                const LocalVariable &v = f.variables[j];
                TypeInfo *c = v.type;
                const char *type_name = "(unknown)";
                if (c) {
                    type_name = c->name.c_str();
                }
                printf("  Variable %s at %d of type %s @ %llx\n",
                       v.name.c_str(),
                       v.stack_offset,
                       type_name,
                       (long long unsigned)v.type_def_loc);
                for (size_t k = 0; k < v.live_ranges.size(); k++) {
                    printf("    Live range: %llx - %llx\n",
                           (unsigned long long)v.live_ranges[k].pc_begin,
                           (unsigned long long)v.live_ranges[k].pc_end);
                }
            }
        }

        // Dump the pc -> source file relationship
        for (size_t i = 0; i < source_lines.size(); i++) {
            printf("%p -> %s:%d\n",
                   (void *)(source_lines[i].pc),
                   source_files[source_lines[i].file].c_str(),
                   source_lines[i].line);
        }
    }

private:

    void load_and_parse_object_file(const std::string &binary) {
        llvm::object::ObjectFile *obj = NULL;

        // Open the object file in question. The API to do this keeps changing.
        #if LLVM_VERSION >= 36

        llvm::ErrorOr<llvm::object::OwningBinary<llvm::object::ObjectFile>> maybe_obj =
            llvm::object::ObjectFile::createObjectFile(binary);

        if (!maybe_obj) {
            debug(1) << "Failed to load binary:" << binary << "\n";
            return;
        }

        obj = maybe_obj.get().getBinary();

        #elif LLVM_VERSION >= 35

        llvm::ErrorOr<std::unique_ptr<llvm::object::ObjectFile>> maybe_obj =
            llvm::object::ObjectFile::createObjectFile(binary);

        if (!maybe_obj) {
            debug(1) << "Failed to load binary:" << binary << "\n";
            return;
        }

        obj = maybe_obj.get().get();

        #else
        obj = llvm::object::ObjectFile::createObjectFile(binary);
        #endif

        if (obj) {
            working = true;
            parse_object_file(obj);

            #if LLVM_VERSION < 35
            // It's not memory-managed in older LLVMs.
            delete obj;
            #endif
        } else {
            debug(1) << "Could not load object file: " << binary << "\n";
            working = false;
        }
    }

    void parse_object_file(llvm::object::ObjectFile *obj) {
        // Look for the debug_info, debug_abbrev, debug_line, and debug_str sections
        llvm::StringRef debug_info, debug_abbrev, debug_str, debug_line, debug_ranges;

#ifdef __APPLE__
        std::string prefix = "__";
#else
        std::string prefix = ".";
#endif

#if LLVM_VERSION > 34
        for (llvm::object::section_iterator iter = obj->section_begin();
             iter != obj->section_end(); ++iter) {
#else
        llvm::error_code err;
        for (llvm::object::section_iterator iter = obj->begin_sections();
             iter != obj->end_sections(); iter.increment(err)) {
#endif
            llvm::StringRef name;
            iter->getName(name);
            debug(2) << "Section: " << name.str() << "\n";
            if (name == prefix + "debug_info") {
                iter->getContents(debug_info);
            } else if (name == prefix + "debug_abbrev") {
                iter->getContents(debug_abbrev);
            } else if (name == prefix + "debug_str") {
                iter->getContents(debug_str);
            } else if (name == prefix + "debug_line") {
                iter->getContents(debug_line);
            } else if (name == prefix + "debug_ranges") {
                iter->getContents(debug_ranges);
            }
        }

        if (debug_info.empty() ||
            debug_abbrev.empty() ||
            debug_str.empty() ||
            debug_line.empty() ||
            debug_ranges.empty()) {
            debug(2) << "Debugging sections not found\n";
            working = false;
            return;
        }

        {
            // Parse the debug_info section to populate the functions and local variables
            llvm::DataExtractor extractor(debug_info, true, obj->getBytesInAddress());
            llvm::DataExtractor debug_abbrev_extractor(debug_abbrev, true, obj->getBytesInAddress());
            parse_debug_info(extractor, debug_abbrev_extractor, debug_str, debug_ranges);
        }

        {
            llvm::DataExtractor e(debug_line, true, obj->getBytesInAddress());
            parse_debug_line(e);
        }
    }

    void parse_debug_ranges(const llvm::DataExtractor &e) {

    }

    void parse_debug_abbrev(const llvm::DataExtractor &e, uint32_t off = 0) {
        entry_formats.clear();
        while (1) {
            EntryFormat fmt;
            fmt.code = e.getULEB128(&off);
            if (!fmt.code) break;
            fmt.tag = e.getULEB128(&off);
            fmt.has_children = (e.getU8(&off) != 0);
            // Get the attributes
            /*
              printf("code = %lu\n"
              " tag = %lu\n"
              " has_children = %u\n", fmt.code, fmt.tag, fmt.has_children);
            */
            while (1) {
                uint64_t name = e.getULEB128(&off);
                uint64_t form = e.getULEB128(&off);
                if (!name && !form) break;
                //printf(" name = %lu, form = %lu\n", name, form);

                FieldFormat f_fmt(name, form);
                fmt.fields.push_back(f_fmt);
            }
            entry_formats.push_back(fmt);
        }
    }

    void parse_debug_info(const llvm::DataExtractor &e,
                          const llvm::DataExtractor &debug_abbrev,
                          llvm::StringRef debug_str,
                          llvm::StringRef debug_ranges) {
        // Offset into the section
        uint32_t off = 0;

        llvm::StringRef debug_info = e.getData();

        // A constant to use indicating that we don't know the stack
        // offset of a variable.
        const int no_location = 0x80000000;

        while (1) {
            uint64_t start_of_unit_header = off;

            // Parse compilation unit header
            bool dwarf_64;
            uint64_t unit_length = e.getU32(&off);
            if (unit_length == 0xffffffff) {
                dwarf_64 = true;
                unit_length = e.getU64(&off);
            } else {
                dwarf_64 = false;
            }

            if (!unit_length) {
                // A zero-length compilation unit indicates the end of
                // the list.
                break;
            }

            uint64_t start_of_unit = off;

            uint16_t version = e.getU16(&off);
            (void)version;

            uint64_t debug_abbrev_offset = 0;
            if (dwarf_64) {
                debug_abbrev_offset = e.getU64(&off);
            } else {
                debug_abbrev_offset = e.getU32(&off);
            }
            parse_debug_abbrev(debug_abbrev, debug_abbrev_offset);

            uint8_t address_size = e.getU8(&off);

            vector<pair<FunctionInfo, int>> func_stack;
            vector<pair<TypeInfo, int>> type_stack;
            vector<pair<std::string, int>> namespace_stack;
            vector<pair<vector<LiveRange>, int>> live_range_stack;

            int stack_depth = 0;

            uint64_t compile_unit_base_pc = 0;

            // From the dwarf 4 spec
            const unsigned tag_array_type = 0x01;
            const unsigned tag_class_type = 0x02;
            const unsigned tag_lexical_block = 0x0b;
            const unsigned tag_member = 0x0d;
            const unsigned tag_pointer_type = 0x0f;
            const unsigned tag_reference_type = 0x10;
            const unsigned tag_compile_unit = 0x11;
            const unsigned tag_structure_type = 0x13;
            const unsigned tag_typedef = 0x16;
            const unsigned tag_inlined_subroutine = 0x1d;
            const unsigned tag_subrange_type = 0x21;
            const unsigned tag_base_type = 0x24;
            const unsigned tag_const_type = 0x26;
            const unsigned tag_function = 0x2e;
            const unsigned tag_variable = 0x34;
            const unsigned tag_namespace = 0x39;

            const unsigned attr_location = 0x02;
            const unsigned attr_name = 0x03;
            const unsigned attr_byte_size = 0x0b;
            const unsigned attr_low_pc = 0x11;
            const unsigned attr_high_pc = 0x12;
            const unsigned attr_upper_bound = 0x2f;
            const unsigned attr_abstract_origin = 0x31;
            const unsigned attr_data_member_location = 0x38;
            const unsigned attr_frame_base = 0x40;
            const unsigned attr_specification = 0x47;
            const unsigned attr_type = 0x49;
            const unsigned attr_ranges = 0x55;

            while (off - start_of_unit < unit_length) {
                uint64_t location = off;

                // Grab the next debugging information entry
                uint64_t abbrev_code = e.getULEB128(&off);

                // A null entry indicates we're popping the stack.
                if (abbrev_code == 0) {
                    if (func_stack.size() &&
                        stack_depth == func_stack.back().second) {
                        const FunctionInfo &f = func_stack.back().first;
                        functions.push_back(f);
                        func_stack.pop_back();
                    }
                    if (type_stack.size() &&
                        stack_depth == type_stack.back().second) {
                        const TypeInfo &c = type_stack.back().first;
                        types.push_back(c);
                        type_stack.pop_back();
                    }
                    if (namespace_stack.size() &&
                        stack_depth == namespace_stack.back().second) {
                        namespace_stack.pop_back();
                    }
                    if (live_range_stack.size() &&
                        stack_depth == live_range_stack.back().second) {
                        live_range_stack.pop_back();
                    }
                    stack_depth--;
                    continue;
                }

                assert(abbrev_code <= entry_formats.size());
                const EntryFormat &fmt = entry_formats[abbrev_code-1];
                assert(fmt.code == abbrev_code);

                LocalVariable var;
                GlobalVariable gvar;
                FunctionInfo func;
                TypeInfo type_info;
                vector<LiveRange> live_ranges;
                type_info.def_loc = location;
                func.def_loc = location;
                var.def_loc = location;
                gvar.def_loc = location;
                std::string namespace_name;

                std::string containing_namespace;
                if (type_stack.size()) {
                    containing_namespace = type_stack.back().first.name + "::";
                } else {
                    for (size_t i = 0; i < namespace_stack.size(); i++) {
                        containing_namespace += namespace_stack[i].first + "::";
                    }
                }

                var.stack_offset = no_location;

                if (fmt.has_children) {
                    stack_depth++;
                }

                // Track local vars found for this function

                // Grab the fields
                for (size_t i = 0; i < fmt.fields.size(); i++) {
                    unsigned attr = fmt.fields[i].name;

                    // A field can either be a constant value:
                    uint64_t val = 0;
                    // Or a variable length payload:
                    const uint8_t *payload = NULL;
                    // If payload is non-null, val indicates the
                    // payload size. If val is zero the payload is a
                    // null-terminated string.

                    switch(fmt.fields[i].form) {
                    case 1: // addr (4 or 8 bytes)
                    {
                        if (address_size == 4) {
                            val = e.getU32(&off);
                        } else {
                            val = e.getU64(&off);
                        }
                        break;
                    }
                    case 2: // There is no case 2
                    {
                        assert(false && "What's form 2?");
                        break;
                    }
                    case 3: // block2 (2 byte length followed by payload)
                    {
                        val = e.getU16(&off);
                        payload = (const uint8_t *)(debug_info.data() + off);
                        off += val;
                        break;
                    }
                    case 4: // block4 (4 byte length followed by payload)
                    {
                        val = e.getU32(&off);
                        payload = (const uint8_t *)(debug_info.data() + off);
                        off += val;
                        break;
                    }
                    case 5: // data2 (2 bytes)
                    {
                        val = e.getU16(&off);
                        break;
                    }
                    case 6: // data4 (4 bytes)
                    {
                        val = e.getU32(&off);
                        break;
                    }
                    case 7: // data8 (8 bytes)
                    {
                        val = e.getU64(&off);
                        break;
                    }
                    case 8: // string (null terminated sequence of bytes)
                    {
                        val = 0;
                        payload = (const uint8_t *)(debug_info.data() + off);
                        while (e.getU8(&off));
                        break;
                    }
                    case 9: // block (uleb128 length followed by payload)
                    {
                        val = e.getULEB128(&off);
                        payload = (const uint8_t *)(debug_info.data() + off);
                        off += val;
                        break;
                    }
                    case 10: // block1 (1 byte length followed by payload)
                    {
                        val = e.getU8(&off);
                        payload = (const uint8_t *)(debug_info.data() + off);
                        off += val;
                        break;
                    }
                    case 11: // data1 (1 byte)
                    {
                        val = e.getU8(&off);
                        break;
                    }
                    case 12: // flag (1 byte)
                    {
                        val = e.getU8(&off);
                        break;
                    }
                    case 13: // sdata (sleb128 constant)
                    {
                        val = (uint64_t)e.getSLEB128(&off);
                        break;
                    }
                    case 14: // strp (offset into debug_str section. 4 bytes in dwarf 32, 8 in dwarf 64)
                    {
                        uint64_t offset;
                        if (dwarf_64) {
                            offset = e.getU64(&off);
                        } else {
                            offset = e.getU32(&off);
                        }
                        val = 0;
                        payload = (const uint8_t *)(debug_str.data() + offset);
                        break;
                    }
                    case 15: // udata (uleb128 constant)
                    {
                        val = e.getULEB128(&off);
                        break;
                    }
                    case 16: // ref_addr (offset from beginning of debug_info. 4 bytes in dwarf 32, 8 in dwarf 64)
                    {
                        if (dwarf_64) {
                            val = e.getU64(&off);
                        } else {
                            val = e.getU32(&off);
                        }
                        break;
                    }
                    case 17: // ref1 (1 byte offset from the first byte of the compilation unit header)
                    {
                        val = e.getU8(&off) + start_of_unit_header;
                        break;
                    }
                    case 18: // ref2 (2 byte version of the same)
                    {
                        val = e.getU16(&off) + start_of_unit_header;
                        break;
                    }
                    case 19: // ref4 (4 byte version of the same)
                    {
                        val = e.getU32(&off) + start_of_unit_header;
                        break;
                    }
                    case 20: // ref8 (8 byte version of the same)
                    {
                        val = e.getU64(&off) + start_of_unit_header;
                        break;
                    }
                    case 21: // ref_udata (uleb128 version of the same)
                    {
                        val = e.getULEB128(&off) + start_of_unit_header;
                        break;
                    }
                    case 22: // indirect
                    {
                        assert(false && "Can't handle indirect form");
                        break;
                    }
                    case 23: // sec_offset
                    {
                        if (dwarf_64) {
                            val = e.getU64(&off);
                        } else {
                            val = e.getU32(&off);
                        }
                        break;
                    }
                    case 24: // exprloc
                    {
                        // Length
                        val = e.getULEB128(&off);
                        // Payload (contains a DWARF expression to evaluate (ugh))
                        payload = (const uint8_t *)(debug_info.data() + off);
                        off += val;
                        break;
                    }
                    case 25: // flag_present
                    {
                        val = 0;
                        // Just the existence of this field is information apparently? There's no data.
                        break;
                    }
                    case 32: // ref_sig8
                    {
                        // 64-bit type signature for a reference in its own type unit
                        val = e.getU64(&off);
                        break;
                    }
                    default:
                        assert(false && "Unknown form");
                        break;
                    }

                    if (fmt.tag == tag_function) {
                        if (attr == attr_name) {
                            func.name = containing_namespace + std::string((const char *)payload);
                        } else if (attr == attr_low_pc) {
                            func.pc_begin = val;
                        } else if (attr == attr_high_pc) {
                            if (fmt.fields[i].form == 0x1) {
                                // Literal address
                                func.pc_end = val;
                            } else {
                                // Size of the thing
                                func.pc_end = func.pc_begin + val;
                            }
                        } else if (attr == attr_frame_base) {
                            // GCC style
                            if (val == 1 && payload && payload[0] == 0x9c) {
                                func.frame_base = FunctionInfo::GCC;
                            } else if (val == 1 && payload && payload[0] == 0x56 && sizeof(void *) == 8) {
                                func.frame_base = FunctionInfo::ClangFP;
                            } else if (val == 1 && payload && payload[0] == 0x55 && sizeof(void *) == 4) {
                                func.frame_base = FunctionInfo::ClangFP;
                            } else if (val == 1 && payload && payload[0] == 0x57 && sizeof(void *) == 8) {
                                func.frame_base = FunctionInfo::ClangNoFP;
                            } else if (val == 1 && payload && payload[0] == 0x54 && sizeof(void *) == 4) {
                                func.frame_base = FunctionInfo::ClangNoFP;
                            } else {
                                func.frame_base = FunctionInfo::Unknown;
                            }
                        } else if (attr == attr_specification) {
                            func.spec_loc = val;
                        }
                    } else if (fmt.tag == tag_base_type) {
                        if (attr == attr_name) {
                            type_info.name = containing_namespace + std::string((const char *)payload);
                            type_info.type = TypeInfo::Primitive;
                        } else if (attr == attr_byte_size) {
                            type_info.size = val;
                        }
                    } else if (fmt.tag == tag_class_type) {
                        if (attr == attr_name) {
                            type_info.name = containing_namespace + std::string((const char *)payload);
                            type_info.type = TypeInfo::Class;
                        } else if (attr == attr_byte_size) {
                            type_info.size = val;
                        }
                    } else if (fmt.tag == tag_structure_type) {
                        if (attr == attr_name) {
                            type_info.name = containing_namespace + std::string((const char *)payload);
                            type_info.type = TypeInfo::Struct;
                        } else if (attr == attr_byte_size) {
                            type_info.size = val;
                        }
                    } else if (fmt.tag == tag_typedef) {
                        if (attr == attr_name) {
                            type_info.name = containing_namespace + std::string((const char *)payload);
                            type_info.type = TypeInfo::Typedef;
                        } else if (attr == attr_type) {
                            // Approximate a typedef as a single-member class
                            LocalVariable m;
                            m.type_def_loc = val;
                            m.stack_offset = 0;
                            type_info.members.push_back(m);
                        }
                    } else if (fmt.tag == tag_pointer_type) {
                        if (attr == attr_type) {
                            // Approximate a pointer type as a single-member class
                            LocalVariable m;
                            m.type_def_loc = val;
                            m.stack_offset = 0;
                            type_info.members.push_back(m);
                            type_info.type = TypeInfo::Pointer;
                        } else if (attr == attr_byte_size) {
                            // Should really be 4 or 8
                            type_info.size = val;
                        }
                    } else if (fmt.tag == tag_reference_type) {
                        if (attr == attr_type) {
                            LocalVariable m;
                            m.type_def_loc = val;
                            m.stack_offset = 0;
                            type_info.members.push_back(m);
                            type_info.type = TypeInfo::Reference;
                        } else if (attr == attr_byte_size) {
                            // Should really be 4 or 8
                            type_info.size = val;
                        }
                    } else if (fmt.tag == tag_const_type) {
                        if (attr == attr_type) {
                            LocalVariable m;
                            m.type_def_loc = val;
                            m.stack_offset = 0;
                            type_info.members.push_back(m);
                            type_info.type = TypeInfo::Const;
                        } else if (attr == attr_byte_size) {
                            // Should really be 4 or 8
                            type_info.size = val;
                        }
                    } else if (fmt.tag == tag_array_type) {
                        if (attr == attr_type) {
                            LocalVariable m;
                            m.type_def_loc = val;
                            m.stack_offset = 0;
                            type_info.members.push_back(m);
                            type_info.type = TypeInfo::Array;
                        } else if (attr == attr_byte_size) {
                            // According to the dwarf spec, this
                            // should be the number of bytes the array
                            // occupies, but compilers seem to emit
                            // the number of array entries instead.
                            type_info.size = val;
                        }
                    } else if (fmt.tag == tag_variable) {
                        if (attr == attr_name) {
                            if (func_stack.empty()) {
                                // Global var
                                gvar.name = containing_namespace + std::string((const char *)payload);
                            } else {
                                // Either a local var, or a static var inside a function
                                gvar.name = var.name = std::string((const char *)payload);
                            }
                        } else if (attr == attr_location) {
                            // We only understand locations which are
                            // offsets from the function's frame
                            if (payload && payload[0] == 0x91) {
                                // It's a local
                                // payload + 1 is a sleb128
                                var.stack_offset = (int)(get_sleb128(payload+1));
                            } else if (payload && payload[0] == 0x03 && val == (sizeof(void *) + 1)) {
                                // It's a global
                                // payload + 1 is an address
                                const void *addr = *((const void * const *)(payload + 1));
                                gvar.addr = (uint64_t)(addr);
                            } else {
                                // Some other format that we don't understand
                                var.stack_offset = no_location;
                            }
                        } else if (attr == attr_type) {
                            var.type_def_loc = val;
                            gvar.type_def_loc = val;
                        } else if (attr == attr_abstract_origin) {
                            // This is a stack variable imported from a function that was inlined.
                            var.origin_loc = val;
                        } else if (attr == attr_specification) {
                            // This is an instance of a global var with a prototype elsewhere
                            gvar.spec_loc = val;
                        }
                    } else if (fmt.tag == tag_member) {
                        if (attr == attr_name) {
                            var.name = std::string((const char *)payload);
                            if (type_stack.size()) {
                                gvar.name = type_stack.back().first.name + "::" + var.name;
                            } else {
                                gvar.name = var.name;
                            }
                        } else if (attr == attr_data_member_location) {
                            if (!payload) {
                                var.stack_offset = val;
                            } else if (payload[0] == 0x23) {
                                var.stack_offset = (int)(get_uleb128(payload+1));
                            }
                        } else if (attr == attr_type) {
                            var.type_def_loc = val;
                            gvar.type_def_loc = val;
                        }
                    } else if (fmt.tag == tag_namespace) {
                        if (attr == attr_name) {
                            namespace_name = std::string((const char *)payload);
                        }
                    } else if (fmt.tag == tag_subrange_type) {
                        // Could be telling us the size of an array type
                        if (attr == attr_upper_bound &&
                            type_stack.size() &&
                            type_stack.back().first.type == TypeInfo::Array) {
                            type_stack.back().first.size = val+1;
                        }
                    } else if (fmt.tag == tag_inlined_subroutine ||
                               fmt.tag == tag_lexical_block) {
                        if (attr == attr_low_pc) {
                            LiveRange r = {val, val};
                            live_ranges.push_back(r);
                        } else if (attr == attr_high_pc && live_ranges.size()) {
                            if (fmt.fields[i].form == 0x1) {
                                // Literal address
                                live_ranges.back().pc_end = val;
                            } else {
                                // Size
                                live_ranges.back().pc_end = live_ranges.back().pc_begin + val;
                            }
                        } else if (attr == attr_ranges) {
                            if (val < debug_ranges.size()) {
                                // It's an array of addresses
                                const void * const * ptr = (const void * const *)(debug_ranges.data() + val);
                                const void * const * end = (const void * const *)(debug_ranges.data() + debug_ranges.size());
                                while (ptr[0] && ptr < end-1) {
                                    LiveRange r = {(uint64_t)ptr[0], (uint64_t)ptr[1]};
                                    r.pc_begin += compile_unit_base_pc;
                                    r.pc_end += compile_unit_base_pc;
                                    live_ranges.push_back(r);
                                    ptr += 2;
                                }
                            }
                        }
                    } else if (fmt.tag == tag_compile_unit) {
                        if (attr == attr_low_pc) {
                            compile_unit_base_pc = val;
                        }
                    }

                }

                if (fmt.tag == tag_variable) {
                    if (func_stack.size() && !gvar.addr) {
                        if (live_range_stack.size()) {
                            var.live_ranges = live_range_stack.back().first;
                        }
                        func_stack.back().first.variables.push_back(var);
                    } else {
                        global_variables.push_back(gvar);
                    }
                } else if (fmt.tag == tag_member &&
                           type_stack.size()) {
                    if (var.stack_offset == no_location) {
                        // A member with no stack offset location is probably the prototype for a static member
                        global_variables.push_back(gvar);
                    } else {
                        type_stack.back().first.members.push_back(var);
                    }

                } else if (fmt.tag == tag_function) {
                    if (fmt.has_children) {
                        func_stack.push_back(std::make_pair(func, stack_depth));
                    } else {
                        functions.push_back(func);
                    }
                } else if (fmt.tag == tag_class_type ||
                           fmt.tag == tag_structure_type ||
                           fmt.tag == tag_array_type ||
                           fmt.tag == tag_base_type) {
                    if (fmt.has_children) {
                        type_stack.push_back(std::make_pair(type_info, stack_depth));
                    } else {
                        types.push_back(type_info);
                    }
                } else if ((fmt.tag == tag_typedef ||
                            fmt.tag == tag_pointer_type ||
                            fmt.tag == tag_reference_type ||
                            fmt.tag == tag_const_type) &&
                           type_info.members.size() == 1) {
                    types.push_back(type_info);
                } else if (fmt.tag == tag_namespace && fmt.has_children) {
                    if (namespace_name.empty()) {
                        namespace_name = "{anonymous}";
                    }
                    namespace_stack.push_back(std::make_pair(namespace_name, stack_depth));
                } else if ((fmt.tag == tag_inlined_subroutine ||
                            fmt.tag == tag_lexical_block) &&
                           live_ranges.size() && fmt.has_children) {
                    live_range_stack.push_back(std::make_pair(live_ranges, stack_depth));
                }
            }
        }

        // Connect function definitions to their declarations
        {
            std::map<uint64_t, FunctionInfo *> func_map;
            for (size_t i = 0; i < functions.size(); i++) {
                func_map[functions[i].def_loc] = &functions[i];
            }

            for (size_t i = 0; i < functions.size(); i++) {
                if (functions[i].spec_loc) {
                    FunctionInfo *spec = func_map[functions[i].spec_loc];
                    if (spec) {
                        functions[i].name = spec->name;
                    }
                }
            }
        }

        // Connect inlined variable instances to their origins
        {
            std::map<uint64_t, LocalVariable *> var_map;
            for (size_t i = 0; i < functions.size(); i++) {
                for (size_t j = 0; j < functions[i].variables.size(); j++) {
                    var_map[functions[i].variables[j].def_loc] = &(functions[i].variables[j]);
                }
            }

            for (size_t i = 0; i < functions.size(); i++) {
                for (size_t j = 0; j < functions[i].variables.size(); j++) {
                    LocalVariable &v = functions[i].variables[j];
                    uint64_t loc = v.origin_loc;
                    if (loc) {
                        LocalVariable *origin = var_map[loc];
                        if (origin) {
                            v.name = origin->name;
                            v.type = origin->type;
                            v.type_def_loc = origin->type_def_loc;
                        } else {
                            debug(4) << "Variable with bad abstract origin: " << loc << "\n";
                        }
                    }
                }
            }
        }

        // Connect global variable instances to their prototypes
        {
            std::map<uint64_t, GlobalVariable *> var_map;
            for (size_t i = 0; i < global_variables.size(); i++) {
                GlobalVariable &var = global_variables[i];
                debug(4) << "var " << var.name << " is at " << var.def_loc << "\n";
                if (var.spec_loc || var.name.empty()) {
                    // Not a prototype
                    continue;
                }
                var_map[var.def_loc] = &var;
            }

            for (size_t i = 0; i < global_variables.size(); i++) {
                GlobalVariable &var = global_variables[i];
                if (var.name.empty() && var.spec_loc) {
                    GlobalVariable *spec = var_map[var.spec_loc];
                    if (spec) {
                        var.name = spec->name;
                        var.type = spec->type;
                        var.type_def_loc = spec->type_def_loc;
                    } else {
                        debug(4) << "Global variable with bad spec loc: " << var.spec_loc << "\n";
                    }
                }
            }
        }

        // Hook up the type pointers
        {
            std::map<uint64_t, TypeInfo *> type_map;
            for (size_t i = 0; i < types.size(); i++) {
                type_map[types[i].def_loc] = &types[i];
            }

            for (size_t i = 0; i < functions.size(); i++) {
                for (size_t j = 0; j < functions[i].variables.size(); j++) {
                    functions[i].variables[j].type =
                        type_map[functions[i].variables[j].type_def_loc];
                }
            }

            for (size_t i = 0; i < global_variables.size(); i++) {
                global_variables[i].type =
                    type_map[global_variables[i].type_def_loc];
            }

            for (size_t i = 0; i < types.size(); i++) {
                for (size_t j = 0; j < types[i].members.size(); j++) {
                    types[i].members[j].type =
                        type_map[types[i].members[j].type_def_loc];
                }
            }
        }

        for (size_t i = 0; i < types.size(); i++) {
            // Set the names of the pointer types
            vector<std::string> suffix;
            TypeInfo *t = &types[i];
            while (t) {
                if (t->type == TypeInfo::Pointer) {
                    suffix.push_back("*");
                    assert(t->members.size() == 1);
                    t = t->members[0].type;
                } else if (t->type == TypeInfo::Reference) {
                    suffix.push_back("&");
                    assert(t->members.size() == 1);
                    t = t->members[0].type;
                } else if (t->type == TypeInfo::Const) {
                    suffix.push_back("const");
                    assert(t->members.size() == 1);
                    t = t->members[0].type;
                } else if (t->type == TypeInfo::Array) {
                    // Do we know the size?
                    if (t->size != 0) {
                        std::ostringstream oss;
                        oss << '[' << t->size << ']';
                        suffix.push_back(oss.str());
                    } else {
                        suffix.push_back("[]");
                    }
                    assert(t->members.size() == 1);
                    t = t->members[0].type;
                } else {
                    break;
                }
            }

            if (t && suffix.size()) {
                types[i].name = t->name;
                while (suffix.size()) {
                    types[i].name += " " + suffix.back();
                    suffix.pop_back();
                }
            }
        }



        // Unpack class members into the local variables list.
        for (size_t i = 0; i < functions.size(); i++) {
            vector<LocalVariable> new_vars = functions[i].variables;
            for (size_t j = 0; j < new_vars.size(); j++) {
                // If new_vars[j] is a class type, unpack its members
                // immediately after this point.
                const LocalVariable &v = new_vars[j];
                if (v.type &&
                    (v.type->type == TypeInfo::Struct ||
                     v.type->type == TypeInfo::Class ||
                     v.type->type == TypeInfo::Typedef)) {
                    size_t members = v.type->members.size();
                    new_vars.insert(new_vars.begin() + j + 1,
                                    v.type->members.begin(),
                                    v.type->members.end());

                    // Typedefs retain the same name and stack offset
                    if (new_vars[j].type->type == TypeInfo::Typedef) {
                        new_vars[j+1].name = new_vars[j].name;
                        new_vars[j+1].stack_offset = new_vars[j].stack_offset;
                    } else {
                        // Correct the stack offsets and names
                        for (size_t k = 0; k < members; k++) {
                            new_vars[j+k+1].stack_offset += new_vars[j].stack_offset;
                            if (new_vars[j+k+1].name.size() &&
                                new_vars[j].name.size()) {
                                new_vars[j+k+1].name = new_vars[j].name + "." + new_vars[j+k+1].name;
                            }
                        }
                    }
                }
            }
            functions[i].variables.swap(new_vars);

            if (functions[i].variables.size()) {
                debug(4) << "Function " << functions[i].name << ":\n";
                for (size_t j = 0; j < functions[i].variables.size(); j++) {
                    if (functions[i].variables[j].type) {
                        debug(4) << " " << functions[i].variables[j].type->name << " " << functions[i].variables[j].name << "\n";
                    }
                }
            }

        }

        // Unpack class members of global variables
        for (size_t i = 0; i < global_variables.size(); i++) {
            const GlobalVariable &v = global_variables[i];
            if (v.type && v.addr &&
                (v.type->type == TypeInfo::Struct ||
                 v.type->type == TypeInfo::Class ||
                 v.type->type == TypeInfo::Typedef)) {
                debug(4) << "Unpacking members of " << v.name << " at " << std::hex << v.addr << "\n";
                vector<LocalVariable> &members = v.type->members;
                for (size_t j = 0; j < members.size(); j++) {
                    GlobalVariable mem;
                    if (!v.name.empty() && !members[j].name.empty()) {
                        mem.name = v.name + "." + members[j].name;
                    } else {
                        // Might be a member of an anonymous struct?
                        mem.name = members[j].name;
                    }
                    mem.type = members[j].type;
                    mem.type_def_loc = members[j].type_def_loc;
                    mem.addr = v.addr + members[j].stack_offset;
                    debug(4) << " Member " << mem.name << " goes at " << mem.addr << "\n";
                    global_variables.push_back(mem);
                }
                debug(4) << std::dec;
            }
        }

        // Drop functions for which we don't know the program counter,
        // and variables for which we don't know the stack offset,
        // name, or type.
        {
            vector<FunctionInfo> trimmed;
            for (size_t i = 0; i < functions.size(); i++) {
                FunctionInfo &f = functions[i];
                if (!f.pc_begin ||
                    !f.pc_end ||
                    f.name.empty()) {
                    //debug(4) << "Dropping " << f.name << "\n";
                    continue;
                }

                vector<LocalVariable> vars;
                for (size_t j = 0; j < f.variables.size(); j++) {
                    LocalVariable &v = f.variables[j];
                    if (!v.name.empty() && v.type && v.stack_offset != no_location) {
                        vars.push_back(v);
                    } else {
                        //debug(4) << "Dropping " << v.name << "\n";
                    }
                }
                f.variables.clear();
                trimmed.push_back(f);
                trimmed.back().variables = vars;
            }
            std::swap(functions, trimmed);
        }

        // Drop globals for which we don't know the address or name
        {
            vector<GlobalVariable> trimmed;
            for (size_t i = 0; i < global_variables.size(); i++) {
                GlobalVariable &v = global_variables[i];
                if (!v.name.empty() && v.addr) {
                    trimmed.push_back(v);
                }
            }

            std::swap(global_variables, trimmed);
        }

        // Sort the functions list by program counter
        std::sort(functions.begin(), functions.end());

        // Sort the global variables by address
        std::sort(global_variables.begin(), global_variables.end());
    }

    void parse_debug_line(const llvm::DataExtractor &e) {
        uint32_t off = 0;

        // For every compilation unit
        while (1) {
            // Parse the header
            uint32_t unit_length = e.getU32(&off);

            if (unit_length == 0) {
                // No more units
                break;
            }

            uint32_t unit_end = off + unit_length;

            debug(4) << "Parsing compilation unit from " << off << " to " << unit_end << "\n";

            uint16_t version = e.getU16(&off);
            assert(version >= 2);

            uint32_t header_length = e.getU32(&off);
            uint32_t end_header_off = off + header_length;
            uint8_t min_instruction_length = e.getU8(&off);
            uint8_t max_ops_per_instruction = 1;
            if (version >= 4) {
                // This is for VLIW architectures
                max_ops_per_instruction = e.getU8(&off);
            }
            uint8_t default_is_stmt = e.getU8(&off);
            int8_t line_base    = (int8_t)e.getU8(&off);
            uint8_t line_range  = e.getU8(&off);
            uint8_t opcode_base = e.getU8(&off);

            vector<uint8_t> standard_opcode_length(opcode_base);
            for (int i = 1; i < opcode_base; i++) {
                // Note we don't use entry 0
                standard_opcode_length[i] = e.getU8(&off);
            }

            vector<std::string> include_dirs;
            // The current directory is implicitly the first dir.
            include_dirs.push_back(".");
            while (off < end_header_off) {
                const char *s = e.getCStr(&off);
                if (s && s[0]) {
                    include_dirs.push_back(s);
                } else {
                    break;
                }
            }

            // The first source file index for this compilation unit.
            int source_files_base = source_files.size();

            while (off < end_header_off) {
                const char *name = e.getCStr(&off);
                if (name && name[0]) {
                    uint64_t dir = e.getULEB128(&off);
                    uint64_t mod_time = e.getULEB128(&off);
                    uint64_t length = e.getULEB128(&off);
                    (void)mod_time;
                    (void)length;
                    assert(dir <= include_dirs.size());
                    source_files.push_back(include_dirs[dir] + "/" + name);
                } else {
                    break;
                }
            }

            assert(off == end_header_off && "Failed parsing section .debug_line");

            // Now parse the table. It uses a state machine with the following fields:
            struct {
                // Current program counter
                uint64_t address;
                // Which op within that instruction (for VLIW archs)
                uint32_t op_index;
                // File and line index;
                uint32_t file, line, column;
                bool is_stmt, basic_block, end_sequence, prologue_end, epilogue_begin;
                // The ISA of the architecture (e.g. x86-64 vs armv7 vs thumb)
                uint32_t isa;
                // The id of the block to which this line belongs
                uint32_t discriminator;

                void append_row(vector<LineInfo> &lines) {
                    LineInfo l = {address, line, file};
                    lines.push_back(l);
                }
            } state, initial_state;

            // Initialize the state table.
            initial_state.address = 0;
            initial_state.op_index = 0;
            initial_state.file = 0;
            initial_state.line = 1;
            initial_state.column = 0;
            initial_state.is_stmt = default_is_stmt;
            initial_state.basic_block = false;
            initial_state.end_sequence = false;
            initial_state.prologue_end = false;
            initial_state.epilogue_begin = false;
            initial_state.isa = 0;
            initial_state.discriminator = 0;
            state = initial_state;

            // For every sequence.
            while (off < unit_end) {
                uint8_t opcode = e.getU8(&off);

                if (opcode == 0) {
                    // Extended opcodes
                    uint32_t ext_offset = off;
                    uint64_t len = e.getULEB128(&off);
                    uint32_t arg_size = len - (off - ext_offset);
                    uint8_t sub_opcode = e.getU8(&off);
                    switch (sub_opcode) {
                    case 1: // end_sequence
                    {
                        state.end_sequence = true;
                        state.append_row(source_lines);
                        state = initial_state;
                        break;
                    }
                    case 2: // set_address
                    {
                        state.address = e.getAddress(&off);
                        break;
                    }
                    case 3: // define_file
                    {
                        const char *name = e.getCStr(&off);
                        uint64_t dir_index = e.getULEB128(&off);
                        uint64_t mod_time = e.getULEB128(&off);
                        uint64_t length = e.getULEB128(&off);
                        (void)mod_time;
                        (void)length;
                        assert(dir_index < include_dirs.size());
                        source_files.push_back(include_dirs[dir_index] + "/" + name);
                        break;
                    }
                    case 4: // set_discriminator
                    {
                        state.discriminator = e.getULEB128(&off);
                        break;
                    }
                    default: // Some unknown thing. Skip it.
                        off += arg_size;
                    }
                } else if (opcode < opcode_base) {
                    // A standard opcode
                    switch (opcode) {
                    case 1: // copy
                    {
                        state.append_row(source_lines);
                        state.basic_block = false;
                        state.prologue_end = false;
                        state.epilogue_begin = false;
                        state.discriminator = 0;
                        break;
                    }
                    case 2: // advance_pc
                    {
                        uint64_t advance = e.getULEB128(&off);
                        state.address += min_instruction_length * ((state.op_index + advance) / max_ops_per_instruction);
                        state.op_index = (state.op_index + advance) % max_ops_per_instruction;
                        break;
                    }
                    case 3: // advance_line
                    {
                        state.line += e.getSLEB128(&off);
                        break;
                    }
                    case 4: // set_file
                    {
                        state.file = e.getULEB128(&off) - 1 + source_files_base;
                        break;
                    }
                    case 5: // set_column
                    {
                        state.column = e.getULEB128(&off);
                        break;
                    }
                    case 6: // negate_stmt
                    {
                        state.is_stmt = !state.is_stmt;
                        break;
                    }
                    case 7: // set_basic_block
                    {
                        state.basic_block = true;
                        break;
                    }
                    case 8: // const_add_pc
                    {
                        // Same as special opcode 255 (but doesn't emit a row or reset state)
                        uint8_t adjust_opcode = 255 - opcode_base;
                        uint64_t advance = adjust_opcode / line_range;
                        state.address += min_instruction_length * ((state.op_index + advance) / max_ops_per_instruction);
                        state.op_index = (state.op_index + advance) % max_ops_per_instruction;
                        break;
                    }
                    case 9: // fixed_advance_pc
                    {
                        uint16_t advance = e.getU16(&off);
                        state.address += advance;
                        break;
                    }
                    case 10: // set_prologue_end
                    {
                        state.prologue_end = true;
                        break;
                    }
                    case 11: // set_epilogue_begin
                    {
                        state.epilogue_begin = true;
                        break;
                    }
                    case 12: // set_isa
                    {
                        state.isa = e.getULEB128(&off);
                        break;
                    }
                    default:
                    {
                        // Unknown standard opcode. Skip over the args.
                        uint8_t args = standard_opcode_length[opcode];
                        for (int i = 0; i < args; i++) {
                            e.getULEB128(&off);
                        }
                    }}
                } else {
                    // Special opcode
                    uint8_t adjust_opcode = opcode - opcode_base;
                    uint64_t advance_op = adjust_opcode / line_range;
                    uint64_t advance_line = line_base + adjust_opcode % line_range;
                    state.address += min_instruction_length * ((state.op_index + advance_op) / max_ops_per_instruction);
                    state.op_index = (state.op_index + advance_op) % max_ops_per_instruction;
                    state.line += advance_line;
                    state.append_row(source_lines);
                    state.basic_block = false;
                    state.prologue_end = false;
                    state.epilogue_begin = false;
                    state.discriminator = 0;
                }
            }
        }

        // Sort the sequences and functions by low PC to make searching into it faster.
        std::sort(source_lines.begin(), source_lines.end());

    }



    FunctionInfo *find_containing_function(void *addr) {
        uint64_t address = (uint64_t)addr;
        debug(4) << "Searching for function containing address " << addr << "\n";
        size_t hi = functions.size();
        size_t lo = 0;
        while (hi > lo) {
            size_t mid = (hi + lo)/2;
            uint64_t pc_mid_begin = functions[mid].pc_begin;
            uint64_t pc_mid_end = functions[mid].pc_end;
            if (address < pc_mid_begin) {
                hi = mid;
            } else if (address > pc_mid_end) {
                lo = mid + 1;
            } else {
                debug(4) << "At function " << functions[mid].name
                         << " spanning: " << (void *)pc_mid_begin
                         << ", " << (void *)pc_mid_end << "\n";
                return &functions[mid];
            }
        }

        return NULL;
    }

    int64_t get_sleb128(const uint8_t *ptr) {
        int64_t result = 0;
        unsigned shift = 0;
        uint8_t byte = 0;

        while (1) {
            assert(shift < 57);
            byte = *ptr++;
            result |= (uint64_t)(byte & 0x7f) << shift;
            shift += 7;
            if ((byte & 0x80) == 0) {
                break;
            }
        }

        // Second-highest bit of the final byte gives the sign.
        if (shift < 64 && (byte & 0x40)) {
            // Fill the rest of the bytes with ones.
            result |= -(1ULL << shift);
        }

        return result;
    }

    int64_t get_uleb128(const uint8_t *ptr) {
        uint64_t result = 0;
        unsigned shift = 0;
        uint8_t byte = 0;

        while (1) {
            assert(shift < 57);
            byte = *ptr++;
            result |= (uint64_t)(byte & 0x7f) << shift;
            shift += 7;
            if ((byte & 0x80) == 0) {
                return result;
            }
        }
    }
};

namespace {
DebugSections *debug_sections = NULL;
}

std::string get_variable_name(const void *var, const std::string &expected_type) {
    if (!debug_sections) return "";
    if (!debug_sections->working) return "";
    std::string name = debug_sections->get_stack_variable_name(var, expected_type);
    if (name.empty()) {
        // Maybe it's a member of a heap object.
        name = debug_sections->get_heap_member_name(var, expected_type);
    }
    if (name.empty()) {
        // Maybe it's a global
        name = debug_sections->get_global_variable_name(var, expected_type);
    }

    return name;
}

std::string get_source_location() {
    if (!debug_sections) return "";
    if (!debug_sections->working) return "";
    return debug_sections->get_source_location();
}

void register_heap_object(const void *obj, size_t size, const void *helper) {
    if (!debug_sections) return;
    if (!debug_sections->working) return;
    if (!helper) return;
    debug_sections->register_heap_object(obj, size, helper);
}

void deregister_heap_object(const void *obj, size_t size) {
    if (!debug_sections) return;
    if (!debug_sections->working) return;
    debug_sections->deregister_heap_object(obj, size);
}

bool saves_frame_pointer(void *fn) {
    // On x86-64, if we save the frame pointer, the first two instructions should be pushing the stack pointer and the frame pointer:
    const uint8_t *ptr = (const uint8_t *)(fn);
    return ptr[0] == 0x55; // push %rbp
}


void test_compilation_unit(bool (*test)(), void (*calib)()) {
    #ifdef __ARM__
    return;
    #else

    // Skip entirely on arm or 32-bit
    if (sizeof(void *) == 4) {
        return;
    }

    debug(4) << "Testing compilation unit with offset_marker at " << reinterpret_bits<void *>(calib) << "\n";

    if (!debug_sections) {
        char path[2048];
        get_program_name(path, sizeof(path));
        debug_sections = new DebugSections(path);
    }

    if (!saves_frame_pointer(reinterpret_bits<void *>(&test_compilation_unit)) ||
        !saves_frame_pointer(reinterpret_bits<void *>(test))) {
        // Make sure libHalide and the test compilation unit both save the frame pointer
        debug_sections->working = false;
        debug(4) << "Failed because frame pointer not saved\n";
    } else if (debug_sections->working) {
        debug_sections->calibrate_pc_offset(calib);
        if (!debug_sections->working) {
            debug(4) << "Failed because offset calibration failed\n";
            return;
        }

        debug_sections->working = (*test)();
        if (!debug_sections->working) {
            debug(4) << "Failed because test routine failed\n";
            return;
        }

        debug(4) << "Test passed\n";
    }

    #endif
}

}
}
}

#else // WITH_INTROSPECTION

namespace Halide {
namespace Internal {
namespace Introspection {

std::string get_variable_name(const void *var, const std::string &expected_type) {
    return "";
}

std::string get_source_location() {
    return "";
}

void register_heap_object(const void *obj, size_t size, const void *helper) {
}

void deregister_heap_object(const void *obj, size_t size) {
}

void test_compilation_unit(bool (*test)(), void (*calib)()) {
}

}
}
}

#endif
