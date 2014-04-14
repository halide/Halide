#include "Introspection.h"
#include "Debug.h"
#include "LLVM_Headers.h"

#include <string>
#include <iostream>
#include <sstream>

// defines backtrace, which gets the call stack as instruction pointers
#include <execinfo.h>

namespace Halide {
namespace Internal {

#ifdef __APPLE__
extern "C" char *__progname;
#define program_invocation_name __progname
#else
// glibc defines the binary name for us
extern "C" char *program_invocation_name;
#endif

class DebugSections {

public:

    bool working;

    DebugSections(std::string binary) : working(false) {
        #ifdef __APPLE__
        binary += ".dSYM/Contents/Resources/DWARF/" + binary;
        #endif

        debug(2) << "Loading " << binary << "\n";

        llvm::object::ObjectFile *obj = load_object_file(binary);
        if (obj) {
            working = true;
            parse_object_file(obj);
            delete obj;
        } else {
            debug(1) << "Could not load object file: " << binary << "\n";
            working = false;
        }

    }

    void calibrate_pc_offset(void (*fn)()) {
        // Calibrate for the offset between the
        // instruction pointers in the debug info and the instruction
        // pointers in the actual file.
        bool found = false;
        int64_t pc_adjust = 0;
        for (size_t i = 0; i < functions.size(); i++) {
            if (functions[i].name == "HalideIntrospectionCanary::offset_marker" &&
                functions[i].pc_begin) {
                uint64_t pc_lo = functions[i].pc_begin;
                uint64_t pc_hi = functions[i].pc_end;
                uint64_t pc = (uint64_t)fn;
                // Find a multiple of 4096 that places pc between pc_lo and pc_hi
                int64_t off = pc - pc_lo;
                off -= off & 0xfff;
                uint64_t pc_adj = pc - off;
                if (pc_lo <= pc_adj &&
                    pc_adj <= pc_hi &&
                    pc_adj + 4096 > pc_hi &&
                    pc_adj - 4096 < pc_lo) {
                    found = true;
                    pc_adjust = off;
                } else {
                    debug(1) << "Found HalideIntrospectionCanary::offset_marker "
                             << "but could not compute a unique offset\n";
                    working = false;
                    return;
                }
            }
        }

        if (!found) {
            debug(1) << "Failed to find HalideIntrospectionCanary::offset_marker\n";
            working = false;
            return;
        }

        for (size_t i = 0; i < functions.size(); i++) {
            functions[i].pc_begin += pc_adjust;
            functions[i].pc_end += pc_adjust;
        }

        for (size_t i = 0; i < source_lines.size(); i++) {
            source_lines[i].pc += pc_adjust;
        }
    }

    // Get the debug name of a stack variable from a pointer to it
    std::string get_variable_name(const void *stack_pointer, const std::string &type_name = "") {

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

        // Get the backtrace
        std::vector<void *> trace(1024);
        int trace_size = backtrace(&trace[0], 1024);
        trace.resize(trace_size);

        // OS X with no frame pointer will return a backtrace of size 1 always.
        if (trace.size() == 1) {
            debug(1) << "backtrace has only one frame\n";
            return "";
        }

        const int addr_size = (int)(sizeof(void *));

        // Begin the search by rounding down the stack pointer to an aligned address
        uint64_t aligned_stack_pointer = (uint64_t)(stack_pointer) & (~(addr_size - 1));

        const void **stack_ptr = (const void **)aligned_stack_pointer;
        // This stack pointer doesn't belong to this function, so there
        // must have been a function call to get here, which means there
        // must be a return address on the stack somewhere down-stack from
        // the pointer.

        // Walk up and down the stack looking for a return address in our backtrace
        const void **stack_frame_top = NULL, **stack_frame_bottom = NULL;
        int frame = 0;
        for (int i = 0; i < 10240 && stack_frame_top == NULL; i++) {
            if (stack_ptr[i] == trace.back()) {
                // Don't walk off the end of the stack
                break;
            }
            for (int j = 1; j < (int)trace.size() && frame == 0; j++) {
                if (stack_ptr[i] == trace[j]) {
                    stack_frame_top = stack_ptr + i;
                    frame = j;
                }
            }
        }

        if (!frame) {
            debug(1) << "Couldn't find which stack frame "
                     << stack_pointer
                     << " belongs to\n";
            return "";
        }

        for (int i = 0; i < 10240 && stack_frame_bottom == NULL; i++) {
            if (stack_ptr[-i] == trace[frame-1]) {
                stack_frame_bottom = stack_ptr - i;
                break;
            }
        }

        // Now what is its offset in that function's frame? The return
        // address is always at the top of the frame.
        int offset_above = (int)((int64_t)(stack_pointer) - (int64_t)(stack_frame_top));
        int offset_below = (int)((int64_t)(stack_pointer) - (int64_t)(stack_frame_bottom));

        uint64_t pc = (uint64_t)trace[frame-1];

        for (size_t i = 0; i < functions.size(); i++) {
            const FunctionInfo &func = functions[i];

            int offset;
            if (func.frame_base == FunctionInfo::GCC) {
                offset = offset_above - addr_size;
            } else if (func.frame_base == FunctionInfo::ClangFP) {
                offset = offset_above + addr_size;
            } else if (func.frame_base == FunctionInfo::ClangNoFP) {
                offset = offset_below - addr_size;
            }

            if (func.pc_begin <= pc &&
                func.pc_end >= pc) {
                for (size_t j = 0; j < func.variables.size(); j++) {
                    const LocalVariable &var = func.variables[j];
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
                         (type && // Check the type matches
                          type->name == type_name))) {
                        return var.name;
                    } else if (elem_type && // Check if it's an array element
                               (type_name.empty() ||
                                (elem_type && // Check the type matches
                                 elem_type->name == type_name))) {
                        int64_t array_size_bytes = type->size * elem_type->size;
                        int64_t pos_bytes = offset - var.stack_offset;
                        if (pos_bytes >= 0 &&
                            pos_bytes < array_size_bytes &&
                            pos_bytes % elem_type->size == 0) {
                            std::ostringstream oss;
                            oss << var.name << '[' << (pos_bytes / elem_type->size) << ']';
                            return oss.str();
                        }
                    }
                }
            }
        }

        return "";
    }

    // Look up n stack frames and get the source location as filename:line
    std::string get_source_location() {

        if (!source_lines.size()) {
            return "";
        }

        const int max_stack_frames = 16;

        // Get the backtrace
        std::vector<void *> trace(max_stack_frames);
        int trace_size = backtrace(&trace[0], (int)(trace.size()));

        for (int frame = 2; frame < trace_size; frame++) {
            uint64_t address = (uint64_t)trace[frame];

            // The actual address of the call is probably 5 bytes earlier (using callq)
            const uint8_t *inst_ptr = (const uint8_t *)address;
            inst_ptr -= 5;
            if (inst_ptr[0] == 0xe8) {
                // TODO: Check x86-32, arm
                // Found the callq
                address -= 5;
            } else {
                return "";
            }

            // Binary search into functions
            size_t hi = functions.size();
            size_t lo = 0;
            while (hi > lo + 1) {
                size_t mid = (hi + lo)/2;
                uint64_t pc_mid_begin = functions[mid].pc_begin;
                uint64_t pc_mid_end = functions[mid].pc_end;
                if (address < pc_mid_begin) {
                    hi = mid;
                } else if (address > pc_mid_end) {
                    lo = mid + 1;
                } else {
                    hi = lo = mid;
                    break;
                }
            }

            // If we're still in the Halide namespace, continue searching
            if (functions.size() &&
                functions[lo].name.size() > 8 &&
                functions[lo].name.substr(0, 8) == "Halide::") {
                continue;
            }

            // Binary search into source_lines
            hi = source_lines.size();
            lo = 0;
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
            return oss.str();
        }

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
            printf("Function %s at %llx - %llx (frame_base %d): \n",
                   functions[i].name.c_str(),
                   (unsigned long long)(functions[i].pc_begin),
                   (unsigned long long)(functions[i].pc_end),
                   (int)functions[i].frame_base);
            for (size_t j = 0; j < functions[i].variables.size(); j++) {
                TypeInfo *c = functions[i].variables[j].type;
                const char *type_name = "(unknown)";
                if (c) {
                    type_name = c->name.c_str();
                }
                printf("  Variable %s at %d of type %s @ %llx\n",
                       functions[i].variables[j].name.c_str(),
                       functions[i].variables[j].stack_offset,
                       type_name,
                       (long long unsigned)functions[i].variables[j].type_def_loc);
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

    llvm::object::ObjectFile *load_object_file(const std::string &binary) {
        // Open the object file in question
        llvm::ErrorOr<llvm::object::ObjectFile *> maybe_obj =
            llvm::object::ObjectFile::createObjectFile(binary);

        if (!maybe_obj) {
            debug(1) << "Failed to load binary:" << binary << "\n";
            return NULL;
        }

        return maybe_obj.get();
    }

    void parse_object_file(llvm::object::ObjectFile *obj) {
        // Look for the debug_info, debug_abbrev, debug_line, and debug_str sections
        llvm::StringRef debug_info, debug_abbrev, debug_str, debug_line;

#ifdef __APPLE__
        std::string prefix = "__";
#else
        std::string prefix = ".";
#endif

        for (llvm::object::section_iterator iter = obj->section_begin();
             iter != obj->section_end(); ++iter) {
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
            }
        }

        if (debug_info.empty() ||
            debug_abbrev.empty() ||
            debug_str.empty() ||
            debug_line.empty()) {
            debug(2) << "Debugging sections not found\n";
            working = false;
            return;
        }

        {
            // Parse the debug abbrev section to populate the entry formats
            llvm::DataExtractor e(debug_abbrev, obj->isLittleEndian(), obj->getBytesInAddress());
            parse_debug_abbrev(e);
        }

        {
            // Parse the debug_info section to populate the functions and local variables
            llvm::DataExtractor e(debug_info, obj->isLittleEndian(), obj->getBytesInAddress());
            parse_debug_info(e, debug_str);
        }

        {
            llvm::DataExtractor e(debug_line, obj->isLittleEndian(), obj->getBytesInAddress());
            parse_debug_line(e);
        }
    }


    void parse_debug_abbrev(const llvm::DataExtractor &e) {
        // Offset into the section
        uint32_t off = 0;

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

    void parse_debug_info(const llvm::DataExtractor &e, llvm::StringRef debug_str) {
        // Offset into the section
        uint32_t off = 0;

        llvm::StringRef debug_info = e.getData();

        // A constant to use indicating that we don't know the stack
        // offset of a variable.
        const int no_location = 0x80000000;

        while (1) {
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

            if (dwarf_64) {
                uint64_t debug_abbrev_offset = e.getU64(&off);
                (void)debug_abbrev_offset;
            } else {
                uint32_t debug_abbrev_offset = e.getU32(&off);
                (void)debug_abbrev_offset;
            }

            uint8_t address_size = e.getU8(&off);

            std::vector<std::pair<FunctionInfo, int> > func_stack;
            std::vector<std::pair<TypeInfo, int> > type_stack;
            std::vector<std::pair<std::string, int> > namespace_stack;

            int stack_depth = 0;

            // From the dwarf 4 spec
            const unsigned tag_function = 0x2e;
            const unsigned tag_variable = 0x34;
            const unsigned tag_class_type = 0x02;
            const unsigned tag_structure_type = 0x13;
            const unsigned tag_member = 0x0d;
            const unsigned tag_base_type = 0x24;
            const unsigned tag_typedef = 0x16;
            const unsigned tag_namespace = 0x39;
            const unsigned tag_pointer_type = 0x0f;
            const unsigned tag_const_type = 0x26;
            const unsigned tag_reference_type = 0x10;
            const unsigned tag_array_type = 0x01;
            const unsigned tag_subrange_type = 0x21;
            const unsigned attr_name = 0x03;
            const unsigned attr_specification = 0x47;
            const unsigned attr_low_pc = 0x11;
            const unsigned attr_high_pc = 0x12;
            const unsigned attr_location = 0x02;
            const unsigned attr_frame_base = 0x40;
            const unsigned attr_data_member_location = 0x38;
            const unsigned attr_byte_size = 0x0b;
            const unsigned attr_type = 0x49;
            const unsigned attr_upper_bound = 0x2f;
            const unsigned attr_abstract_origin = 0x31;

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
                    stack_depth--;
                    continue;
                }

                assert(abbrev_code <= entry_formats.size());
                const EntryFormat &fmt = entry_formats[abbrev_code-1];
                assert(fmt.code == abbrev_code);

                LocalVariable var;
                FunctionInfo func;
                TypeInfo type_info;
                type_info.def_loc = location;
                func.def_loc = location;
                var.def_loc = location;
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
                        val = e.getU8(&off);
                        break;
                    }
                    case 18: // ref2 (2 byte version of the same)
                    {
                        val = e.getU16(&off);
                        break;
                    }
                    case 19: // ref4 (4 byte version of the same)
                    {
                        val = e.getU32(&off);
                        break;
                    }
                    case 20: // ref8 (8 byte version of the same)
                    {
                        val = e.getU64(&off);
                        break;
                    }
                    case 21: // ref_udata (uleb128 version of the same)
                    {
                        val = e.getULEB128(&off);
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
                    case 26: // ref_sig8
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
                            type_info.size = val;
                        }
                    } else if (fmt.tag == tag_variable) {
                        if (attr == attr_name) {
                            var.name = std::string((const char *)payload);
                        } else if (attr == attr_location) {
                            // We only understand locations which are
                            // offsets from the function's frame
                            if (!payload || payload[0] != 0x91) {
                                var.stack_offset = no_location;
                            } else {
                                // payload + 1 is a sleb128
                                var.stack_offset = (int)(get_sleb128(payload+1));
                            }
                        } else if (attr == attr_type) {
                            var.type_def_loc = val;
                        } else if (attr == attr_abstract_origin) {
                            // This is a stack variable imported from a function that was inlined.
                            var.origin_loc = val;
                        }

                    } else if (fmt.tag == tag_member) {
                        if (attr == attr_name) {
                            var.name = std::string((const char *)payload);
                        } else if (attr == attr_data_member_location) {
                            if (!payload) {
                                var.stack_offset = val;
                            } else if (payload[0] == 0x23) {
                                var.stack_offset = (int)(get_uleb128(payload+1));
                            }
                        } else if (attr == attr_type) {
                            var.type_def_loc = val;
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
                    }

                }

                if (fmt.tag == tag_variable &&
                    func_stack.size()) {
                    func_stack.back().first.variables.push_back(var);
                } else if (fmt.tag == tag_member &&
                           type_stack.size() &&
                           var.stack_offset != no_location) {
                    type_stack.back().first.members.push_back(var);
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
                    debug(4) << functions[i].variables[j].name << " is at " << functions[i].variables[j].def_loc << "\n";
                    var_map[functions[i].variables[j].def_loc] = &(functions[i].variables[j]);
                }
            }

            for (size_t i = 0; i < functions.size(); i++) {
                for (size_t j = 0; j < functions[i].variables.size(); j++) {
                    LocalVariable &v = functions[i].variables[j];
                    uint64_t loc = v.origin_loc;
                    if (loc) {
                        debug(4) << "Origin is at " << loc << "\n";
                        LocalVariable *origin = var_map[loc];
                        if (origin) {
                            v.name = origin->name;
                            v.type = origin->type;
                            v.type_def_loc = origin->type_def_loc;
                        } else {
                            debug(4) << "Could not find origin!\n";
                        }
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

            for (size_t i = 0; i < types.size(); i++) {
                for (size_t j = 0; j < types[i].members.size(); j++) {
                    types[i].members[j].type =
                        type_map[types[i].members[j].type_def_loc];
                }
            }
        }

        for (size_t i = 0; i < types.size(); i++) {
            // Set the names of the pointer types
            std::vector<std::string> suffix;
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
            std::vector<LocalVariable> new_vars = functions[i].variables;
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
            functions[i].variables.swap(new_vars);
        }

        // Drop functions for which we don't know the program counter,
        // and variables for which we don't know the stack offset,
        // name, or type.
        std::vector<FunctionInfo> trimmed;
        for (size_t i = 0; i < functions.size(); i++) {
            FunctionInfo &f = functions[i];
            if (!f.pc_begin ||
                !f.pc_end ||
                f.name.empty()) {
                debug(4) << "Dropping " << f.name << "\n";
                continue;
            }

            std::vector<LocalVariable> vars;
            for (size_t j = 0; j < f.variables.size(); j++) {
                LocalVariable &v = f.variables[j];
                if (!v.name.empty() && v.type && v.stack_offset != no_location) {
                    vars.push_back(v);
                } else {
                    debug(4) << "Dropping " << v.name << "\n";
                }
            }
            f.variables.clear();
            trimmed.push_back(f);
            trimmed.back().variables = vars;
        }
        std::swap(functions, trimmed);

        // Sort the functions list by program counter
        std::sort(functions.begin(), functions.end());

    }

    void parse_debug_line(const llvm::DataExtractor &e) {
        uint32_t off = 0;

        // Parse the header
        uint32_t unit_length = e.getU32(&off);
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

        std::vector<uint8_t> standard_opcode_length(opcode_base);
        for (int i = 1; i < opcode_base; i++) {
            // Note we don't use entry 0
            standard_opcode_length[i] = e.getU8(&off);
        }

        std::vector<std::string> include_dirs;
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

            void append_row(std::vector<LineInfo> &lines) {
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
        while (off < unit_length) {
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
                    state.file = e.getULEB128(&off) - 1;
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

        // Sort the sequences and functions by low PC to make searching into it faster.
        std::sort(source_lines.begin(), source_lines.end());

    }

    struct FieldFormat {
        uint64_t name, form;
        FieldFormat() : name(0), form(0) {}
        FieldFormat(uint64_t n, uint64_t f) : name(n), form(f) {}
    };

    struct EntryFormat {
        uint64_t code, tag;
        bool has_children;
        EntryFormat() : code(0), tag(0), has_children(false) {}
        std::vector<FieldFormat> fields;
    };
    std::vector<EntryFormat> entry_formats;

    struct TypeInfo;

    struct LocalVariable {
        std::string name;
        TypeInfo *type;
        int stack_offset;
        uint64_t type_def_loc;
        uint64_t def_loc, origin_loc;
        LocalVariable() : name(""), type(NULL), stack_offset(0), type_def_loc(0), def_loc(0), origin_loc(0) {}
    };

    struct FunctionInfo {
        std::string name;
        uint64_t pc_begin, pc_end;
        std::vector<LocalVariable> variables;
        uint64_t def_loc, spec_loc;
        // The stack variable offsets are w.r.t either:
        // gcc: the top of the stack frame (one below the return address to the caller)
        // clang with frame pointers: the bottom of the stack frame (one above the return address to this function)
        // clang without frame pointers: the top of the stack frame (...TODO...)
        enum {Unknown = 0, GCC, ClangFP, ClangNoFP} frame_base;
        FunctionInfo() : name(""), pc_begin(0), pc_end(0), def_loc(0), spec_loc(0) {}

        bool operator<(const FunctionInfo &other) {
            return pc_begin < other.pc_begin;
        }
    };
    std::vector<FunctionInfo> functions;

    std::vector<std::string> source_files;
    struct LineInfo {
        uint64_t pc;
        uint32_t line;
        uint32_t file; // Index into source_files
        bool operator<(const LineInfo &other) const {
            return pc < other.pc;
        }
    };
    std::vector<LineInfo> source_lines;

    struct TypeInfo {
        std::string name;
        uint64_t size;
        uint64_t def_loc;
        std::vector<LocalVariable> members;

        // TypeInfo can also be used to represent a pointer to
        // another type, in which case there's a single member, which
        // represents the value pointed to (its name is empty and its
        // stack_offset is meaningless).
        enum {Primitive, Class, Struct, Pointer, Typedef, Const, Reference, Array} type;

        TypeInfo() : size(0), def_loc(0), type(Primitive) {}
    };
    std::vector<TypeInfo> types;

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
    assert(debug_sections);
    return debug_sections->get_variable_name(var, expected_type);
}

std::string get_source_location() {
    assert(debug_sections);
    return debug_sections->get_source_location();
}

void test_compilation_unit(bool (*test)(), void (*calib)()) {
    if (!debug_sections) {
        debug_sections = new DebugSections(program_invocation_name);
    }
    debug_sections->calibrate_pc_offset(calib);
    if (debug_sections->working) {
        debug_sections->working = (*test)();
    }
}

}
}


