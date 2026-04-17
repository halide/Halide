#ifndef HALIDE_TILE_IR_H
#define HALIDE_TILE_IR_H

/** \file
 * Self-contained binary encoder for NVIDIA Tile IR bytecode format.
 * Format based on the cuda-tile open-source project (Apache 2.0 + LLVM
 * Exceptions). See https://github.com/NVIDIA/cuda-tile
 *
 * NOTE: This file is only used internally for CodeGen. Do NOT add it
 * to the list of exported Halide headers.
 *
 * == TILE IR BYTECODE FORMAT GUIDE ==
 *
 * Tile IR is a binary IR format used by NVIDIA's tile compiler for GPU
 * tensor accelerators (sm_100+/Blackwell). All values are tiles
 * (multi-dimensional arrays); scalars are rank-0 tiles. The bytecode
 * can be passed directly to cuModuleLoadData on supported hardware.
 *
 * INTEGERS: All variable-length integers use unsigned LEB128 encoding
 * (called "PrefixVarInt" in the cuda-tile source). Each byte uses the
 * high bit as a continuation flag: if set, more bytes follow.
 *
 * MODULE LAYOUT:
 *   Magic:   8 bytes  {0x7F, 'T', 'i', 'l', 'e', 'I', 'R', 0x00}
 *   Version: 4 bytes  {major(u8), minor(u8), tag(u16-LE)}
 *   Sections: repeated until EndOfBytecode
 *   EndOfBytecode: single byte 0x00
 *
 * SECTION FORMAT:
 *   section_id(u8)   Low 7 bits = ID, high bit = hasAlignment
 *   size(varint)     Total byte size of section body
 *   [align(varint)]  Only if hasAlignment bit is set; alignment in bytes
 *   [padding]        0xCB bytes to reach alignment boundary
 *   body(bytes)      Section-specific content
 *
 * Section IDs: EndOfBytecode=0, String=1, Func=2, Debug=3,
 *              Constant=4, Type=5, Global=6
 *
 * TYPE SECTION (ID=5):
 *   numTypes(varint)
 *   [padding to alignment]
 *   offsets[numTypes](u32-LE each)  Byte offset into type data
 *   type data:
 *     Each type starts with a TypeTag byte:
 *       I1=0x00, I8=0x01, I16=0x02, I32=0x03, I64=0x04
 *       F16=0x05, BF16=0x06, F32=0x07, F64=0x09
 *       Pointer=0x0C: pointee_type_idx(varint)
 *       Tile=0x0D:    element_type_idx(varint), rank(varint),
 *                      dims[rank](varint each)
 *       Function=0x10: num_params(varint), param_type_idxs(varint each),
 *                       num_results(varint), result_type_idxs(varint each)
 *       Token=0x11
 *
 * STRING SECTION (ID=1):
 *   numStrings(varint)
 *   [padding to alignment]
 *   offsets[numStrings](u32-LE each)  Byte offset into string blob
 *   string blob: concatenated raw strings (no null terminators)
 *   String lengths are derived from offset differences.
 *
 * CONSTANT SECTION (ID=4):
 *   numConstants(varint)
 *   [padding to alignment]
 *   offsets[numConstants](u32-LE each)  Byte offset into constant data
 *   constant data: concatenated raw constant blobs
 *
 * FUNCTION SECTION (ID=2):
 *   numFunctions(varint)
 *   For each function:
 *     name_string_idx(varint)
 *     func_type_idx(varint)
 *     flags(u8):  bit0=visibility(0=public,1=private)
 *                 bit1=kind(0=device,1=kernel)
 *                 bit2=hasOptHints
 *     [optHints if hasOptHints]
 *     functionLocIndex(varint)
 *     bodySize(varint)
 *     body(bytes): sequence of ops
 *   [padding to alignment]
 *
 * FUNCTION BODY (sequence of ops):
 *   The body is a flat sequence of operations. Each op produces zero or
 *   more SSA results, numbered sequentially starting from 0 within the
 *   function (including block arguments). Operands reference results by
 *   their SSA ID (varint).
 *
 * OP ENCODING (per cuda-tile-tblgen BytecodeReaderGen/WriterGen):
 *   opcode(varint)
 *   [numResults(varint)]  -- ONLY if Operator::isVariadic() is true.
 *       isVariadic() returns true when the op has ANY Variadic-typed
 *       operands OR results in its ODS definition. This is the most
 *       critical encoding detail: even ops with zero results (like
 *       ReturnOp, YieldOp) need this field if they have Variadic
 *       operands.
 *   resultTypeIndices: one varint per result (count known from ODS or
 *       numResults)
 *   [flags(varint)]  -- present if op has any Optional or UnitAttr
 *       fields. Each optional/unit attr gets one bit. If all bits are
 *       0, some ops omit the flags byte entirely (depends on op).
 *   attributes: required attributes in ODS declaration order.
 *       Enum attrs are inline varints. Self-contained attrs (like
 *       DenseElements) use AttrTag + data.
 *   operands: required operands as varint SSA IDs. Variadic operands
 *       are preceded by a count varint.
 *   [regions]: for ops with regions (ForOp, IfOp, ReduceOp).
 *       Each region contains one or more blocks. Each block:
 *         numBlockArgs(varint)
 *         blockArgTypes[](varint type index each)
 *         numOps(varint)
 *         ops: nested op sequence
 *
 * KEY TILE IR CONSTRAINTS:
 *   - All tile dimensions must be powers of two.
 *   - BroadcastOp: AllRanksMatch<["source","result"]>. To broadcast a
 *     scalar to a vector, first ReshapeOp scalar->tile<1xT>, then
 *     BroadcastOp tile<1xT>->tile<NxT>.
 *   - OffsetOp: AllTypesMatch<["result","ptr"]>. For vector access,
 *     broadcast the scalar ptr to a vector of ptrs first.
 *   - ExtractOp: AllRanksMatch<["source","result"]>. Extracting from a
 *     1D tile gives a 1D result (tile<1xT>); reshape to scalar if needed.
 *   - CatOp: AllRanksMatch<["lhs","rhs","result"]>. All operands and
 *     result must have the same rank.
 *   - FToIOp: Only RoundNearestIntToZero (value 6) is supported.
 *
 * EXAMPLES (common ops):
 *   ConstantOp (0x10): resultType(vi) value(self-contained attr)
 *   AddIOp (0x03):     resultType(vi) overflow(vi) lhs(vi) rhs(vi)
 *   AddFOp (0x02):     resultType(vi) flags(vi) [rounding(vi)] lhs(vi) rhs(vi)
 *   ReturnOp (0x5C):   numResults=0(vi) numOperands(vi) operands(vi each)
 *   ForOp (0x29):      numResults(vi) resultTypes(vi each) lb(vi) ub(vi)
 *                       step(vi) numInits(vi) inits(vi each) bodyRegion
 *   IfOp (0x32):        numResults(vi) resultTypes(vi each) cond(vi)
 *                       thenRegion [elseRegion if numResults>0 or flag]
 *
 * (vi = varint)
 */

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Type.h"

namespace Halide {
namespace Internal {
namespace TileIR {

// --- Binary format constants (from NVIDIA cuda-tile) ---

static const uint8_t kMagic[] = {0x7F, 'T', 'i', 'l', 'e', 'I', 'R', 0x00};
static const uint8_t kVersionMajor = 13;
static const uint8_t kVersionMinor = 1;
static const uint16_t kVersionTag = 0;

// Alignment padding byte
static const uint8_t kAlignmentByte = 0xCB;

// Section IDs (low 7 bits; high bit = hasAlignment)
enum SectionID : uint8_t {
    SectionEndOfBytecode = 0x00,
    SectionString = 0x01,
    SectionFunc = 0x02,
    SectionDebug = 0x03,
    SectionConstant = 0x04,
    SectionType = 0x05,
    SectionGlobal = 0x06,
};

// Scalar type tags
enum TypeTag : uint8_t {
    TypeI1 = 0x00,
    TypeI8 = 0x01,
    TypeI16 = 0x02,
    TypeI32 = 0x03,
    TypeI64 = 0x04,
    TypeF16 = 0x05,
    TypeBF16 = 0x06,
    TypeF32 = 0x07,
    TypeTF32 = 0x08,
    TypeF64 = 0x09,
    TypeF8E4M3FN = 0x0A,
    TypeF8E5M2 = 0x0B,
    TypePointer = 0x0C,
    TypeTile = 0x0D,
    TypeTensorView = 0x0E,
    TypePartitionView = 0x0F,
    TypeFunction = 0x10,
    TypeToken = 0x11,
};

// Opcodes (FROZEN assignments from BytecodeOpcodes.td)
enum Opcode : uint8_t {
    OpAbsFOp = 0x00,
    OpAbsIOp = 0x01,
    OpAddFOp = 0x02,
    OpAddIOp = 0x03,
    OpAndIOp = 0x04,
    OpAssertOp = 0x05,
    OpAssumeOp = 0x06,
    OpAtomicCASTkoOp = 0x07,
    OpAtomicRMWTkoOp = 0x08,
    OpBitcastOp = 0x09,
    OpBreakOp = 0x0A,
    OpBroadcastOp = 0x0B,
    OpCatOp = 0x0C,
    OpCeilOp = 0x0D,
    OpCmpFOp = 0x0E,
    OpCmpIOp = 0x0F,
    OpConstantOp = 0x10,
    OpContinueOp = 0x11,
    OpCosOp = 0x12,
    OpCosHOp = 0x13,
    OpDivFOp = 0x14,
    OpDivIOp = 0x15,
    OpEntryOp = 0x16,
    OpExpOp = 0x17,
    OpExp2Op = 0x18,
    OpExtIOp = 0x25,
    OpExtractOp = 0x26,
    OpFloorOp = 0x27,
    OpFmaOp = 0x28,
    OpForOp = 0x29,
    OpFToFOp = 0x2A,
    OpFToIOp = 0x2B,
    OpGetGlobalOp = 0x2C,
    OpGetIndexSpaceShapeOp = 0x2D,
    OpGetNumTileBlocksOp = 0x2E,
    OpGetTensorShapeOp = 0x2F,
    OpGetTileBlockIdOp = 0x30,
    OpGlobalOp = 0x31,
    OpIfOp = 0x32,
    OpIntToPtrOp = 0x33,
    OpIotaOp = 0x3A,
    OpIToFOp = 0x3B,
    OpJoinTokensOp = 0x3C,
    OpLoadPtrTkoOp = 0x3D,
    OpLoadViewTkoOp = 0x3E,
    OpLogOp = 0x3F,
    OpLog2Op = 0x40,
    OpLoopOp = 0x41,
    OpMakePartitionViewOp = 0x42,
    OpMakeTensorViewOp = 0x43,
    OpMakeTokenOp = 0x44,
    OpMaxFOp = 0x45,
    OpMaxIOp = 0x46,
    OpMinFOp = 0x47,
    OpMinIOp = 0x48,
    OpMmaFOp = 0x49,
    OpMmaIOp = 0x4A,
    OpModuleOp = 0x4B,
    OpMulFOp = 0x4C,
    OpMulhiIOp = 0x4D,
    OpMulIOp = 0x4E,
    OpNegFOp = 0x4F,
    OpNegIOp = 0x50,
    OpOffsetOp = 0x51,
    OpOrIOp = 0x52,
    OpPermuteOp = 0x53,
    OpPowOp = 0x54,
    OpPrintOp = 0x55,
    OpPtrToIntOp = 0x56,
    OpPtrToPtrOp = 0x57,
    OpReduceOp = 0x58,
    OpRemFOp = 0x59,
    OpRemIOp = 0x5A,
    OpReshapeOp = 0x5B,
    OpReturnOp = 0x5C,
    OpRsqrtOp = 0x5D,
    OpScanOp = 0x5E,
    OpSelectOp = 0x5F,
    OpShLIOp = 0x60,
    OpShRIOp = 0x61,
    OpSinOp = 0x62,
    OpSinHOp = 0x63,
    OpSqrtOp = 0x64,
    OpStorePtrTkoOp = 0x65,
    OpStoreViewTkoOp = 0x66,
    OpSubFOp = 0x67,
    OpSubIOp = 0x68,
    OpTanOp = 0x69,
    OpTanHOp = 0x6A,
    OpTruncIOp = 0x6B,
    OpXOrIOp = 0x6C,
    OpYieldOp = 0x6D,
};

// --- Attribute enums (from cuda-tile AttrDefs.td) ---
// These are written as inline varints for enum-typed attributes.

// ComparisonPredicate: shared by CmpIOp and CmpFOp
enum ComparisonPredicate : uint32_t {
    CmpEqual = 0,
    CmpNotEqual = 1,
    CmpLessThan = 2,
    CmpLessThanOrEqual = 3,
    CmpGreaterThan = 4,
    CmpGreaterThanOrEqual = 5,
};

// ComparisonOrdering: used by CmpFOp
enum ComparisonOrdering : uint32_t {
    CmpUnordered = 0,
    CmpOrdered = 1,
};

// Signedness: used by CmpIOp, DivIOp, RemIOp, ShRIOp, ExtIOp, IToFOp, FToIOp
enum Signedness : uint32_t {
    SignednessUnsigned = 0,
    SignednessSigned = 1,
};

// RoundingMode: used by float ops, conversion ops
enum RoundingMode : uint32_t {
    RoundNearestEven = 0,
    RoundZero = 1,
    RoundNegativeInf = 2,
    RoundPositiveInf = 3,
    RoundApprox = 4,
    RoundFull = 5,
    RoundNearestIntToZero = 6,
};

// IntegerOverflow: used by AddIOp, SubIOp, MulIOp, ShLIOp, TruncIOp
enum IntegerOverflow : uint32_t {
    OverflowNone = 0,
    OverflowNSW = 1,
    OverflowNUW = 2,
    OverflowNW = 3,
};

// MemoryOrderingSemantics: used by LoadPtrTkoOp, StorePtrTkoOp
enum MemoryOrderingSemantics : uint32_t {
    MemOrderWeak = 0,
    MemOrderRelaxed = 1,
    MemOrderAcquire = 2,
    MemOrderRelease = 3,
    MemOrderAcqRel = 4,
};

// ReduceKind: used by ReduceOp body (not an inline enum, encoded via body region)
// Kept for reference but ReduceOp uses a region body, not an enum attribute
enum ReduceKind : uint64_t {
    ReduceAdd = 0,
    ReduceMax = 1,
    ReduceMin = 2,
    ReduceMul = 3,
};

// Function entry flags
enum FuncFlag : uint8_t {
    FuncVisibilityPrivate = 0x01,  // Bit 0: 0=public, 1=private
    FuncKindKernel = 0x02,         // Bit 1: 0=device func, 1=kernel entry
    FuncHasOptHints = 0x04,        // Bit 2: has optimization hints
};

// Attribute tags for self-contained attributes
enum AttributeTag : uint8_t {
    AttrInteger = 1,
    AttrFloat = 2,
    AttrBool = 3,
    AttrType = 4,
    AttrString = 5,
    AttrArray = 6,
    AttrDenseElements = 7,
};

/** Low-level binary writer using LEB128 varint encoding. */
class Encoder {
public:
    void write_byte(uint8_t b);
    void write_bytes(const uint8_t *data, size_t len);
    void write_varint(uint64_t val);
    void write_signed_varint(int64_t val);
    void write_le_u8(uint8_t val);
    void write_le_u16(uint16_t val);
    void write_le_u32(uint32_t val);
    void write_le_i32(int32_t val);
    void write_le_i64(int64_t val);
    void write_le_u64(uint64_t val);
    void write_le_f32(float val);
    void write_le_f64(double val);
    void write_string(const std::string &s);
    void align_to(uint64_t alignment);

    const std::vector<uint8_t> &data() const {
        return buf;
    }

    size_t size() const {
        return buf.size();
    }

private:
    std::vector<uint8_t> buf;
};

/** String table with deduplication and offset-array encoding. */
class StringTable {
public:
    uint32_t add(const std::string &s);
    void encode(Encoder &enc) const;

private:
    std::vector<std::string> strings;
    std::map<std::string, uint32_t> index_map;
};

/** Type table mapping Halide types to Tile IR type indices. */
class TypeTable {
public:
    uint32_t add_scalar(const Halide::Type &t);
    uint32_t add_tile(const std::vector<int64_t> &shape, uint32_t scalar_type_idx);
    uint32_t add_pointer(uint32_t pointee_type_idx);
    uint32_t add_token();
    uint32_t add_function(uint32_t num_params, const std::vector<uint32_t> &param_type_idxs,
                          uint32_t num_results, const std::vector<uint32_t> &result_type_idxs);
    void encode(Encoder &enc) const;

    /** Get the type index for a Halide type, creating tile types as needed.
     * In Tile IR, all values are tiles. Scalar Halide types become rank-0 tiles. */
    uint32_t get_type_idx(const Halide::Type &t);

    /** Get a rank-1 tile type index with explicit lane count. Unlike get_type_idx,
     * this always creates a 1D tile even for lanes==1 (tile<1xT> vs scalar tile<T>).
     * Pads to next power of 2. */
    uint32_t get_1d_tile_type_idx(const Halide::Type &elem_type, int lanes);

    /** Get a rank-1 tile type with exact lane count (no power-of-2 padding).
     * Use for CatOp results where the size must equal sum of input sizes. */
    uint32_t get_exact_1d_tile_type_idx(const Halide::Type &elem_type, int lanes);

private:
    struct TypeEntry {
        TypeTag tag;
        std::vector<int64_t> shape;
        uint32_t aux1 = 0;
        std::vector<uint32_t> params;
        uint32_t num_results = 0;
        std::vector<uint32_t> result_idxs;
    };
    std::vector<TypeEntry> types;
    std::map<std::string, uint32_t> type_map;

    std::string make_key(TypeTag tag, uint32_t aux1 = 0,
                         const std::vector<int64_t> &shape = {},
                         const std::vector<uint32_t> &params = {}) const;
};

/** Constant pool for DenseElements attributes (SectionConstant). */
class ConstantPool {
public:
    /** Add a constant (raw bytes). Returns the pool index. */
    uint32_t add(const std::vector<uint8_t> &raw_data);

    /** Encode the constant section into enc. Empty pool = no section emitted. */
    void encode(Encoder &enc) const;

    bool empty() const { return entries.empty(); }

private:
    std::vector<std::vector<uint8_t>> entries;
};

/** A function being assembled. */
class Function {
public:
    std::string name;
    uint32_t name_idx = 0;
    uint32_t func_type_idx = 0;
    uint8_t flags = 0;
    Encoder body;
    uint32_t next_ssa_id = 0;

    uint32_t alloc_id() {
        return next_ssa_id++;
    }
};

/** Top-level Tile IR module. */
class Module {
public:
    StringTable strings;
    TypeTable types;
    ConstantPool constants;
    std::vector<Function> functions;

    /** Serialize the complete module into output. */
    void encode(std::vector<char> &output) const;
};

}  // namespace TileIR
}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_TILE_IR_H
