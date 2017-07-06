// This file provides a means to load/store Buffers from/to HDF5 multi-dimensional data files
// (see https://support.hdfgroup.org/HDF5/).

#ifndef HALIDE_HDF5_IO_H
#define HALIDE_HDF5_IO_H

#include "H5Cpp.h"
#include "HalideRuntime.h"
#include <vector>
#include <map>
#include <string>
#include <assert.h>
#include <cstdarg>

namespace Halide {
namespace Tools {

namespace {

enum Endianness {
    UseNative = 0,
    BigEndian,
    LittleEndian
};

void type_error(const halide_type_t &type) {
    fprintf(stderr, "Error: HDF5 I/O cannot handle data with type: %d bits: %d\n", type.code, type.bits);
    exit(-1);
}

template<typename T>
std::string type_to_string() {
    halide_type_t type = halide_type_of<T>();
    return ("<Type with Halide typecode " + std::to_string(type.code) + ", " + std::to_string(type.bits) + ">");
}

template<> std::string type_to_string<int8_t>()   { return "int8";   }
template<> std::string type_to_string<int16_t>()  { return "int16";  }
template<> std::string type_to_string<int32_t>()  { return "int32";  }
template<> std::string type_to_string<int64_t>()  { return "int64";  }
template<> std::string type_to_string<uint8_t>()  { return "uint8";  }
template<> std::string type_to_string<uint16_t>() { return "uint16"; }
template<> std::string type_to_string<uint32_t>() { return "uint32"; }
template<> std::string type_to_string<uint64_t>() { return "uint64"; }
template<> std::string type_to_string<float>()    { return "float";  }
template<> std::string type_to_string<double>()   { return "double"; }

std::string hdf5_type_to_string(H5::DataType &type) {
    if(type == H5::PredType::NATIVE_INT8) return "int8 (NATIVE_INT8)";
    else if(type == H5::PredType::STD_I8BE) return "int8 (STD_I8BE)";
    else if(type == H5::PredType::STD_I8LE) return "int8 (STD_I8LE)";
    else if(type == H5::PredType::NATIVE_INT16) return "int16 (NATIVE_INT16)";
    else if(type == H5::PredType::STD_I16BE) return "int16 (STD_I16BE)";
    else if(type == H5::PredType::STD_I16LE) return "int16 (STD_I16LE)";
    else if(type == H5::PredType::NATIVE_INT32) return "int32 (NATIVE_INT32)";
    else if(type == H5::PredType::STD_I32BE) return "int32 (STD_I32BE)";
    else if(type == H5::PredType::STD_I32LE) return "int32 (STD_I32LE)";
    else if(type == H5::PredType::NATIVE_INT64) return "int64 (NATIVE_INT64)";
    else if(type == H5::PredType::STD_I64BE) return "int64 (STD_I64BE)";
    else if(type == H5::PredType::STD_I64LE) return "int64 (STD_I64LE)";
    else if(type == H5::PredType::NATIVE_UINT8) return "uint8 (NATIVE_UINT8)";
    else if(type == H5::PredType::STD_U8BE) return "uint8 (STD_U8BE)";
    else if(type == H5::PredType::STD_U8LE) return "uint8 (STD_U8LE)";
    else if(type == H5::PredType::NATIVE_UINT16) return "uint16 (NATIVE_UINT16)";
    else if(type == H5::PredType::STD_U16BE) return "uint16 (STD_U16BE)";
    else if(type == H5::PredType::STD_U16LE) return "uint16 (STD_U16LE)";
    else if(type == H5::PredType::NATIVE_UINT32) return "uint32 (NATIVE_UINT32)";
    else if(type == H5::PredType::STD_U32BE) return "uint32 (STD_U32BE)";
    else if(type == H5::PredType::STD_U32LE) return "uint32 (STD_U32LE)";
    else if(type == H5::PredType::NATIVE_UINT64) return "uint64 (NATIVE_UINT64)";
    else if(type == H5::PredType::STD_U64BE) return "uint64 (STD_U64BE)";
    else if(type == H5::PredType::STD_U64LE) return "uint64 (STD_U64LE)";
    else if(type == H5::PredType::NATIVE_FLOAT) return "float (NATIVE_FLOAT)";
    else if(type == H5::PredType::IEEE_F32BE) return "float (IEEE_F32BE)";
    else if(type == H5::PredType::IEEE_F32LE) return "float (IEEE_F32LE)";

    return "<unsupported HDF5 data type>";
}

template<typename T> bool type_match(H5::DataType hdf5_type) { return false; }

template<> bool type_match<int8_t>(H5::DataType hdf5_type) {
    return (hdf5_type == H5::PredType::NATIVE_INT8) ||
            (hdf5_type == H5::PredType::STD_I8BE) ||
            (hdf5_type == H5::PredType::STD_I8LE);
}
template<> bool type_match<int16_t>(H5::DataType hdf5_type) {
    return (hdf5_type == H5::PredType::NATIVE_INT16) ||
            (hdf5_type == H5::PredType::STD_I16BE) ||
            (hdf5_type == H5::PredType::STD_I16LE);
}
template<> bool type_match<int32_t>(H5::DataType hdf5_type) {
    return (hdf5_type == H5::PredType::NATIVE_INT32) ||
            (hdf5_type == H5::PredType::STD_I32BE) ||
            (hdf5_type == H5::PredType::STD_I32LE);
}
template<> bool type_match<int64_t>(H5::DataType hdf5_type) {
    return (hdf5_type == H5::PredType::NATIVE_INT64) ||
            (hdf5_type == H5::PredType::STD_I64BE) ||
            (hdf5_type == H5::PredType::STD_I64LE);
}
template<> bool type_match<uint8_t>(H5::DataType hdf5_type) {
    return (hdf5_type == H5::PredType::NATIVE_UINT8) ||
            (hdf5_type == H5::PredType::STD_U8BE) ||
            (hdf5_type == H5::PredType::STD_U8LE);
}
template<> bool type_match<uint16_t>(H5::DataType hdf5_type) {
    return (hdf5_type == H5::PredType::NATIVE_UINT16) ||
            (hdf5_type == H5::PredType::STD_U16BE) ||
            (hdf5_type == H5::PredType::STD_U16LE);
}
template<> bool type_match<uint32_t>(H5::DataType hdf5_type) {
    return (hdf5_type == H5::PredType::NATIVE_UINT32) ||
            (hdf5_type == H5::PredType::STD_U32BE) ||
            (hdf5_type == H5::PredType::STD_U32LE);
}
template<> bool type_match<uint64_t>(H5::DataType hdf5_type) {
    return (hdf5_type == H5::PredType::NATIVE_UINT64) ||
            (hdf5_type == H5::PredType::STD_U64BE) ||
            (hdf5_type == H5::PredType::STD_U64LE);
}
template<> bool type_match<float>(H5::DataType hdf5_type) {
    return (hdf5_type == H5::PredType::NATIVE_FLOAT) ||
            (hdf5_type == H5::PredType::IEEE_F32BE) ||
            (hdf5_type == H5::PredType::IEEE_F32LE);
}

template<typename T>
void type_load_match_error(H5::DataType hdf5_type, std::string buffer_name) {
    fprintf(stderr, "Error: type of buffer found in HDF5 file does not match requested Buffer type.\n  Buffer: %s\n  Requested type: %s\n  Found type: %s\n",
            buffer_name.c_str(),
            type_to_string<T>().c_str(),
            hdf5_type_to_string(hdf5_type).c_str()
            );
    exit(-1);
}

//Get the HDF5 library's type descriptor matching a Halide type descriptor.
H5::DataType hdf5_type_from_halide_type(const halide_type_t &type, Endianness endian = UseNative) {
    switch(type.code) {
    case halide_type_int:
        switch(type.bits) {
        case 8:
            switch(endian) {
            case UseNative: return H5::PredType::NATIVE_INT8;
            case BigEndian: return H5::PredType::STD_I8BE;
            case LittleEndian: return H5::PredType::STD_I8LE;
            }
            break;
        case 16:
            switch(endian) {
            case UseNative: return H5::PredType::NATIVE_INT16;
            case BigEndian: return H5::PredType::STD_I16BE;
            case LittleEndian: return H5::PredType::STD_I16LE;
            }
            break;
        case 32:
            switch(endian) {
            case UseNative: return H5::PredType::NATIVE_INT32;
            case BigEndian: return H5::PredType::STD_I32BE;
            case LittleEndian: return H5::PredType::STD_I32LE;
            }
            break;
        case 64:
            switch(endian) {
            case UseNative: return H5::PredType::NATIVE_INT64;
            case BigEndian: return H5::PredType::STD_I64BE;
            case LittleEndian: return H5::PredType::STD_I64LE;
            }
            break;
        default:
            type_error(type);
        }
        break;
    case halide_type_uint:
        switch(type.bits) {
        case 8:
            switch(endian) {
            case UseNative: return H5::PredType::NATIVE_UINT8;
            case BigEndian: return H5::PredType::STD_U8BE;
            case LittleEndian: return H5::PredType::STD_U8LE;
            }
            break;
        case 16:
            switch(endian) {
            case UseNative: return H5::PredType::NATIVE_UINT16;
            case BigEndian: return H5::PredType::STD_U16BE;
            case LittleEndian: return H5::PredType::STD_U16LE;
            }
            break;
        case 32:
            switch(endian) {
            case UseNative: return H5::PredType::NATIVE_UINT32;
            case BigEndian: return H5::PredType::STD_U32BE;
            case LittleEndian: return H5::PredType::STD_U32LE;
            }
            break;
        case 64:
            switch(endian) {
            case UseNative: return H5::PredType::NATIVE_UINT64;
            case BigEndian: return H5::PredType::STD_U64BE;
            case LittleEndian: return H5::PredType::STD_U64LE;
            }
            break;
        default:
            type_error(type);
        }
        break;
    case halide_type_float:
        switch(type.bits) {
        case 32:
            switch(endian) {
            case UseNative: return H5::PredType::NATIVE_FLOAT;
            case BigEndian: return H5::PredType::IEEE_F32BE;
            case LittleEndian: return H5::PredType::IEEE_F32LE;
            }
            break;
        default:
            type_error(type);
        }
        break;
    default:
        type_error(type);
    }

    return H5::PredType::NATIVE_INT;
}

//Copy elements from one raw buffer to another, with a specified amount of dimensions, and extents + strides for each dimension.
template<typename ElemType>
void copy_elems(ElemType *src, ElemType *dst, int dimensions, int *extents, int *src_strides, int *dst_strides) {
    //If 0 dimensions, do the copy.
    if(dimensions == 0) {
        dst[0] = src[0];
        return;
    }

    //If non-0 dimensions, recursively call self to reduce dimensionality
    for(int loc = 0; loc<extents[0]; loc++) {
        ElemType *_src = src + (loc * dst_strides[0]);
        ElemType *_dst = dst + (loc * dst_strides[0]);
        copy_elems(_src, _dst, dimensions-1, &(extents[1]), &(src_strides[1]), &(dst_strides[1]));
    }
}

//Create a dense data buffer from the contents of a Halide buffer. The returned pointer should be freed externally.
template<typename BufferType>
void * create_dense_buffer(BufferType &buf) {
    using ElemType = typename BufferType::ElemType;

    assert(buf.dimensions() <= 16);

    //Allocate a dense data buffer.
    size_t buf_size_elems = buf.number_of_elements();
    ElemType * retval = (ElemType *) malloc(buf_size_elems * sizeof(ElemType));

    //Store extent, stride information about the buffer.
    int extents[16];
    int src_strides[16];
    int dst_strides[16];
    for(int dim = 0; dim<buf.dimensions(); dim++) {
        if(dim == 0) { dst_strides[dim] = 1; }
        else { dst_strides[dim] = extents[dim-1] * dst_strides[dim-1]; }
        src_strides[dim] = buf.dim(buf.dimensions()-1-dim).stride();
        extents[dim] = buf.dim(buf.dimensions()-1-dim).extent();
    }

    //Do the copy.
    copy_elems(buf.data(), retval, buf.dimensions(), extents, src_strides, dst_strides);

    return (void*) retval;
}

//Fill the values of a Halide buffer with the contents of a dense data buffer. The shape of the Halide buffer
//has to be pre-defined and its storage should already be allocated.
template<typename BufferType>
void fill_from_dense_buffer(BufferType &buf, void *data) {
    using ElemType = typename BufferType::ElemType;

    assert(buf.dimensions() <= 16);

    //Store extent, stride information about the buffer.
    int extents[16];
    int src_strides[16];
    int dst_strides[16];
    for(int dim = 0; dim<buf.dimensions(); dim++) {
        if(dim == 0) { src_strides[dim] = 1; }
        else { src_strides[dim] = extents[dim-1] * src_strides[dim-1]; }
        dst_strides[dim] = buf.dim(buf.dimensions()-1-dim).stride();
        extents[dim] = buf.dim(buf.dimensions()-1-dim).extent();
    }

    //Do the copy.
    copy_elems((ElemType *)data, buf.data(), buf.dimensions(), extents, src_strides, dst_strides);
}

//Add a single Buffer to an open HDF5 file.
template<typename BufferType>
void add_to_hdf5(H5::H5File &file, std::vector<std::string> names, int name_idx, BufferType buffer) {
    using ElemType = typename BufferType::ElemType;

    assert(names.size() > (unsigned)name_idx);
    assert(buffer.dimensions() <= 16);

    hsize_t dims[16];
    for(int i=0; i<=buffer.dimensions(); i++){
        dims[i] = buffer.dim(buffer.dimensions()-1-i).extent();
    }

    H5::DataSpace dataspace(buffer.dimensions(), dims);
    H5::DataSet dataset = file.createDataSet(names[name_idx],
                                             hdf5_type_from_halide_type(halide_type_of<ElemType>(), BigEndian),
                                             dataspace);

    ElemType *data = (ElemType *) create_dense_buffer<BufferType>(buffer);
    dataset.write(data, hdf5_type_from_halide_type(halide_type_of<ElemType>(), UseNative));
    free(data);
}

//Add a set of Buffers to an open HDF5 file.
template<typename BufferType, typename... NextBufferTypes>
void add_to_hdf5(H5::H5File &file, std::vector<std::string> names, int name_idx, BufferType buffer, NextBufferTypes... next_buffers) {
    add_to_hdf5(file, names, name_idx, buffer);
    add_to_hdf5(file, names, name_idx+1, next_buffers...);
}

}

/** Save a (set of) Halide buffer(s) into an HDF5 file and close it.
 * Usage examples:
 *
 * Buffer<uint16_t> buf1;
 * Buffer<float> buf2;
 *
 * save_hdf5( {"buf1_name_in_file"}, "out.h5", buf1 );
 * save_hdf5( {"buf1_name_in_file", "buf2_name_in_file"}, "out_both.h5", buf1, buf2 );
 */
template<typename... BufferTypes>
void save_hdf5(std::vector<std::string> buffer_names, std::string filename, BufferTypes... buffers) {

    try {
        //Open the HDF5 file.
        H5::H5File file(filename, H5F_ACC_TRUNC);

        //Write the buffers into it.
        add_to_hdf5(file, buffer_names, 0, buffers...);
    } catch (std::exception const &error) {
        fprintf(stderr, "%s\n", error.what());
        exit(-1);
    }
}

/**
 * Load a Halide buffer from an HDF5 file.
 * Will abort if the buffer is not found in the file, or if the types of the requested and
 * found buffers do not match.
 */
template<typename BufferType>
BufferType load_from_hdf5(std::string filename, std::string buffer_name) {
    using ElemType = typename BufferType::ElemType;
    BufferType retval;

    try {
        //Open the HDF5 file.
        H5::H5File file(filename, H5F_ACC_RDWR);

        H5::DataSet dataset = file.openDataSet(buffer_name);
        H5::DataSpace dataspace = dataset.getSpace();
        H5::DataType datatype = dataset.getDataType();
        if(!type_match<ElemType>(datatype)) {
            type_load_match_error<ElemType>(datatype, buffer_name);
        }

        int dimensions = dataspace.getSimpleExtentNdims();
        assert(dimensions <= 16);
        hsize_t dims[16];
        std::vector<int> dims_vec;
        assert(dataspace.getSimpleExtentDims(dims, NULL) == dimensions);
        for(int d=0; d<dimensions; d++) {
            dims_vec.insert(dims_vec.begin(), (int)dims[d]);
        }

        retval = BufferType(BufferType::static_halide_type(), dims_vec);
        size_t mem_size = dataset.getInMemDataSize();
        ElemType *data = (ElemType *) malloc(mem_size);

        dataset.read(data, hdf5_type_from_halide_type(halide_type_of<ElemType>(), UseNative));
        retval.allocate();
        fill_from_dense_buffer<BufferType>(retval, (void*)data);

        free(data);
    } catch (std::exception const &error) {
        fprintf(stderr, "%s\n", error.what());
        exit(-1);
    }

    return retval;
}

}
}



#endif
