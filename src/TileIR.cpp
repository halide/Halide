#include "TileIR.h"

#include <cstring>
#include <sstream>

namespace Halide {
namespace Internal {
namespace TileIR {

// --- Encoder (LEB128 varint) ---

void Encoder::write_byte(uint8_t b) {
    buf.push_back(b);
}

void Encoder::write_bytes(const uint8_t *data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

void Encoder::write_varint(uint64_t val) {
    do {
        uint8_t byte = val & 0x7F;
        val >>= 7;
        if (val != 0) {
            byte |= 0x80;
        }
        write_byte(byte);
    } while (val != 0);
}

void Encoder::write_signed_varint(int64_t val) {
    uint64_t uval = (static_cast<uint64_t>(val) << 1) ^
                     static_cast<uint64_t>(val >> 63);
    write_varint(uval);
}

void Encoder::write_le_u8(uint8_t val) {
    write_byte(val);
}

void Encoder::write_le_u16(uint16_t val) {
    write_byte(static_cast<uint8_t>(val & 0xFF));
    write_byte(static_cast<uint8_t>((val >> 8) & 0xFF));
}

void Encoder::write_le_u32(uint32_t val) {
    write_byte(static_cast<uint8_t>(val & 0xFF));
    write_byte(static_cast<uint8_t>((val >> 8) & 0xFF));
    write_byte(static_cast<uint8_t>((val >> 16) & 0xFF));
    write_byte(static_cast<uint8_t>((val >> 24) & 0xFF));
}

void Encoder::write_le_i32(int32_t val) {
    uint32_t uval;
    memcpy(&uval, &val, sizeof(uval));
    write_le_u32(uval);
}

void Encoder::write_le_i64(int64_t val) {
    uint64_t uval;
    memcpy(&uval, &val, sizeof(uval));
    write_le_u64(uval);
}

void Encoder::write_le_u64(uint64_t val) {
    for (int i = 0; i < 8; i++) {
        write_byte(static_cast<uint8_t>(val & 0xFF));
        val >>= 8;
    }
}

void Encoder::write_le_f32(float val) {
    uint32_t uval;
    memcpy(&uval, &val, sizeof(uval));
    write_le_u32(uval);
}

void Encoder::write_le_f64(double val) {
    uint64_t uval;
    memcpy(&uval, &val, sizeof(uval));
    write_le_u64(uval);
}

void Encoder::write_string(const std::string &s) {
    write_varint(s.size());
    write_bytes(reinterpret_cast<const uint8_t *>(s.data()), s.size());
}

void Encoder::align_to(uint64_t alignment) {
    if (alignment < 2) return;
    uint64_t pos = buf.size();
    uint64_t padding = (alignment - (pos % alignment)) % alignment;
    for (uint64_t i = 0; i < padding; i++) {
        write_byte(kAlignmentByte);
    }
}

// --- StringTable ---

uint32_t StringTable::add(const std::string &s) {
    auto it = index_map.find(s);
    if (it != index_map.end()) {
        return it->second;
    }
    uint32_t idx = static_cast<uint32_t>(strings.size());
    strings.push_back(s);
    index_map[s] = idx;
    return idx;
}

void StringTable::encode(Encoder &enc) const {
    Encoder section;
    section.write_varint(strings.size());
    section.align_to(4);

    std::vector<uint32_t> offsets;
    uint32_t running_offset = 0;
    for (const auto &s : strings) {
        offsets.push_back(running_offset);
        running_offset += static_cast<uint32_t>(s.size());
    }
    for (uint32_t off : offsets) {
        section.write_le_u32(off);
    }
    for (const auto &s : strings) {
        section.write_bytes(reinterpret_cast<const uint8_t *>(s.data()), s.size());
    }

    uint8_t id_and_align = SectionString | 0x80;
    enc.write_byte(id_and_align);
    enc.write_varint(section.size());
    enc.write_varint(4);
    enc.align_to(4);
    enc.write_bytes(section.data().data(), section.size());
}

// --- TypeTable ---

std::string TypeTable::make_key(TypeTag tag, uint32_t aux1,
                                const std::vector<int64_t> &shape,
                                const std::vector<uint32_t> &params) const {
    std::ostringstream oss;
    oss << static_cast<int>(tag) << ":" << aux1;
    for (auto s : shape) {
        oss << ":" << s;
    }
    for (auto p : params) {
        oss << ",p" << p;
    }
    return oss.str();
}

uint32_t TypeTable::add_scalar(const Halide::Type &t) {
    TypeTag tag;
    if (t.bits() == 0) {
        tag = TypeI32;
    } else if (t.is_bool()) {
        tag = TypeI1;
    } else if (t.is_int() || t.is_uint()) {
        switch (t.bits()) {
        case 8: tag = TypeI8; break;
        case 16: tag = TypeI16; break;
        case 32: tag = TypeI32; break;
        case 64: tag = TypeI64; break;
        default: tag = TypeI32; break;
        }
    } else if (t.is_float()) {
        switch (t.bits()) {
        case 16: tag = TypeF16; break;
        case 32: tag = TypeF32; break;
        case 64: tag = TypeF64; break;
        default: tag = TypeF32; break;
        }
    } else if (t.is_bfloat()) {
        tag = TypeBF16;
    } else {
        tag = TypeI32;
    }

    std::string key = make_key(tag);
    auto it = type_map.find(key);
    if (it != type_map.end()) {
        return it->second;
    }
    uint32_t idx = static_cast<uint32_t>(types.size());
    TypeEntry entry;
    entry.tag = tag;
    types.push_back(entry);
    type_map[key] = idx;
    return idx;
}

uint32_t TypeTable::add_tile(const std::vector<int64_t> &shape, uint32_t scalar_type_idx) {
    std::string key = make_key(TypeTile, scalar_type_idx, shape);
    auto it = type_map.find(key);
    if (it != type_map.end()) {
        return it->second;
    }
    uint32_t idx = static_cast<uint32_t>(types.size());
    TypeEntry entry;
    entry.tag = TypeTile;
    entry.aux1 = scalar_type_idx;
    entry.shape = shape;
    types.push_back(entry);
    type_map[key] = idx;
    return idx;
}

uint32_t TypeTable::add_pointer(uint32_t pointee_type_idx) {
    std::string key = make_key(TypePointer, pointee_type_idx);
    auto it = type_map.find(key);
    if (it != type_map.end()) {
        return it->second;
    }
    uint32_t idx = static_cast<uint32_t>(types.size());
    TypeEntry entry;
    entry.tag = TypePointer;
    entry.aux1 = pointee_type_idx;
    types.push_back(entry);
    type_map[key] = idx;
    return idx;
}

uint32_t TypeTable::add_token() {
    std::string key = make_key(TypeToken);
    auto it = type_map.find(key);
    if (it != type_map.end()) {
        return it->second;
    }
    uint32_t idx = static_cast<uint32_t>(types.size());
    TypeEntry entry;
    entry.tag = TypeToken;
    types.push_back(entry);
    type_map[key] = idx;
    return idx;
}

uint32_t TypeTable::add_function(uint32_t num_params, const std::vector<uint32_t> &param_type_idxs,
                                 uint32_t num_results, const std::vector<uint32_t> &result_type_idxs) {
    std::string key = make_key(TypeFunction, num_params, {}, param_type_idxs);
    for (auto r : result_type_idxs) {
        key += ",r" + std::to_string(r);
    }
    auto it = type_map.find(key);
    if (it != type_map.end()) {
        return it->second;
    }
    uint32_t idx = static_cast<uint32_t>(types.size());
    TypeEntry entry;
    entry.tag = TypeFunction;
    entry.aux1 = num_params;
    entry.params = param_type_idxs;
    entry.num_results = num_results;
    entry.result_idxs = result_type_idxs;
    types.push_back(entry);
    type_map[key] = idx;
    return idx;
}

// Round up to the next power of two. Returns x if already a power of two.
static int64_t next_power_of_two(int64_t x) {
    int64_t p = 1;
    while (p < x) {
        p *= 2;
    }
    return p;
}

uint32_t TypeTable::get_type_idx(const Halide::Type &t) {
    // In Tile IR, all values are tiles. Scalar Halide types map to
    // scalar tiles (rank-0 tile = tile<scalar_type>), and vector types
    // map to 1-D tiles (tile<Nxscalar_type>).
    // Tile IR requires all tile dimensions to be powers of two.
    // Non-power-of-2 vector widths (from intermediate vectorization)
    // are padded up; the extra lanes contain unused values.
    uint32_t scalar_idx = add_scalar(t.element_of());
    if (t.lanes() > 1) {
        return add_tile({next_power_of_two(t.lanes())}, scalar_idx);
    }
    // Scalar tile = tile with empty shape
    return add_tile({}, scalar_idx);
}

uint32_t TypeTable::get_1d_tile_type_idx(const Halide::Type &elem_type, int lanes) {
    uint32_t scalar_idx = add_scalar(elem_type.element_of());
    return add_tile({next_power_of_two(lanes)}, scalar_idx);
}

uint32_t TypeTable::get_exact_1d_tile_type_idx(const Halide::Type &elem_type, int lanes) {
    uint32_t scalar_idx = add_scalar(elem_type.element_of());
    return add_tile({lanes}, scalar_idx);
}

void TypeTable::encode(Encoder &enc) const {
    Encoder type_data_enc;
    std::vector<uint32_t> offsets;

    for (const auto &t : types) {
        offsets.push_back(static_cast<uint32_t>(type_data_enc.size()));
        type_data_enc.write_byte(static_cast<uint8_t>(t.tag));
        switch (t.tag) {
        case TypeTile:
            type_data_enc.write_varint(t.aux1);
            type_data_enc.write_varint(t.shape.size());
            for (auto dim : t.shape) {
                type_data_enc.write_le_i64(dim);
            }
            break;
        case TypePointer:
            type_data_enc.write_varint(t.aux1);
            break;
        case TypeFunction:
            type_data_enc.write_varint(t.params.size());
            for (auto p : t.params) {
                type_data_enc.write_varint(p);
            }
            type_data_enc.write_varint(t.num_results);
            for (auto r : t.result_idxs) {
                type_data_enc.write_varint(r);
            }
            break;
        default:
            break;
        }
    }

    Encoder section;
    section.write_varint(types.size());
    section.align_to(4);
    for (uint32_t off : offsets) {
        section.write_le_u32(off);
    }
    section.write_bytes(type_data_enc.data().data(), type_data_enc.size());

    uint8_t id_and_align = SectionType | 0x80;
    enc.write_byte(id_and_align);
    enc.write_varint(section.size());
    enc.write_varint(4);
    enc.align_to(4);
    enc.write_bytes(section.data().data(), section.size());
}

// --- ConstantPool ---

uint32_t ConstantPool::add(const std::vector<uint8_t> &raw_data) {
    uint32_t idx = static_cast<uint32_t>(entries.size());
    entries.push_back(raw_data);
    return idx;
}

void ConstantPool::encode(Encoder &enc) const {
    if (entries.empty()) return;

    Encoder section;
    section.write_varint(entries.size());
    section.align_to(8);

    // Compute offsets
    std::vector<uint64_t> offsets;
    uint64_t running = 0;
    for (const auto &entry : entries) {
        offsets.push_back(running);
        // Each entry is: size(varint) + raw_data
        Encoder tmp;
        tmp.write_varint(entry.size());
        running += tmp.size() + entry.size();
    }

    // Write offset array (uint64_t each)
    for (uint64_t off : offsets) {
        section.write_le_u64(off);
    }

    // Write constant data
    for (const auto &entry : entries) {
        section.write_varint(entry.size());
        section.write_bytes(entry.data(), entry.size());
    }

    uint8_t id_and_align = SectionConstant | 0x80;
    enc.write_byte(id_and_align);
    enc.write_varint(section.size());
    enc.write_varint(8);
    enc.align_to(8);
    enc.write_bytes(section.data().data(), section.size());
}

// --- Module ---

void Module::encode(std::vector<char> &output) const {
    Encoder enc;

    // Magic number
    enc.write_bytes(kMagic, sizeof(kMagic));

    // Version: major[u8] minor[u8] tag[u16]
    enc.write_le_u8(kVersionMajor);
    enc.write_le_u8(kVersionMinor);
    enc.write_le_u16(kVersionTag);

    // Type section
    types.encode(enc);

    // String section
    strings.encode(enc);

    // Constant section (before functions, as functions reference pool indices)
    constants.encode(enc);

    // Function table section
    {
        Encoder section;
        section.write_varint(functions.size());

        for (const auto &func : functions) {
            section.write_varint(func.name_idx);
            section.write_varint(func.func_type_idx);
            section.write_byte(func.flags);
            section.write_varint(0);  // functionLocIndex (no debug info)
            section.write_varint(func.body.size());
            section.write_bytes(func.body.data().data(), func.body.size());
        }

        section.align_to(8);

        uint8_t id_and_align = SectionFunc | 0x80;
        enc.write_byte(id_and_align);
        enc.write_varint(section.size());
        enc.write_varint(8);
        enc.align_to(8);
        enc.write_bytes(section.data().data(), section.size());
    }

    // End of bytecode
    enc.write_byte(SectionEndOfBytecode);

    const auto &data = enc.data();
    output.assign(data.begin(), data.end());
}

}  // namespace TileIR
}  // namespace Internal
}  // namespace Halide
