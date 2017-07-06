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
    BigEndianIfPossible,
    LittleEndianIfPossible
};

void type_error(const halide_type_t &type) {
    fprintf(stderr, "HDF5 I/O cannot handle data with type: %d bits: %d\n", type.code, type.bits);
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
            case BigEndianIfPossible: return H5::PredType::STD_I8BE;
            case LittleEndianIfPossible: return H5::PredType::STD_I8LE;
            }
            break;
        case 16:
            switch(endian) {
            case UseNative: return H5::PredType::NATIVE_INT16;
            case BigEndianIfPossible: return H5::PredType::STD_I16BE;
            case LittleEndianIfPossible: return H5::PredType::STD_I16LE;
            }
            break;
        case 32:
            switch(endian) {
            case UseNative: return H5::PredType::NATIVE_INT32;
            case BigEndianIfPossible: return H5::PredType::STD_I32BE;
            case LittleEndianIfPossible: return H5::PredType::STD_I32LE;
            }
            break;
        case 64:
            switch(endian) {
            case UseNative: return H5::PredType::NATIVE_INT64;
            case BigEndianIfPossible: return H5::PredType::STD_I64BE;
            case LittleEndianIfPossible: return H5::PredType::STD_I64LE;
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
            case BigEndianIfPossible: return H5::PredType::STD_U8BE;
            case LittleEndianIfPossible: return H5::PredType::STD_U8LE;
            }
            break;
        case 16:
            switch(endian) {
            case UseNative: return H5::PredType::NATIVE_UINT16;
            case BigEndianIfPossible: return H5::PredType::STD_U16BE;
            case LittleEndianIfPossible: return H5::PredType::STD_U16LE;
            }
            break;
        case 32:
            switch(endian) {
            case UseNative: return H5::PredType::NATIVE_UINT32;
            case BigEndianIfPossible: return H5::PredType::STD_U32BE;
            case LittleEndianIfPossible: return H5::PredType::STD_U32LE;
            }
            break;
        case 64:
            switch(endian) {
            case UseNative: return H5::PredType::NATIVE_UINT64;
            case BigEndianIfPossible: return H5::PredType::STD_U64BE;
            case LittleEndianIfPossible: return H5::PredType::STD_U64LE;
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
            case BigEndianIfPossible: return H5::PredType::INTEL_F32;
            case LittleEndianIfPossible: return H5::PredType::INTEL_F32;
            }
            break;
        case 64:
            switch(endian) {
            case UseNative: return H5::PredType::NATIVE_INT64;
            case BigEndianIfPossible: return H5::PredType::INTEL_F64;
            case LittleEndianIfPossible: return H5::PredType::INTEL_F64;
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
                                             hdf5_type_from_halide_type(halide_type_of<ElemType>(), BigEndianIfPossible),
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

//Save a set of Halide buffers into an HDF5 file.
template<typename... BufferTypes>
bool save_hdf5(std::vector<std::string> buffer_names, std::string filename, BufferTypes... buffers) {
    //Open the HDF5 file.
    H5::H5File file(filename, H5F_ACC_TRUNC);

    //Write the buffers into it.
    add_to_hdf5(file, buffer_names, 0, buffers...);

    return true;
}

//Load a Halide buffer from an HDF5 file.
template<typename BufferType>
BufferType load_from_hdf5(std::string filename, std::string buffer_name) {
    using ElemType = typename BufferType::ElemType;

    //Open the HDF5 file.
    H5::H5File file(filename, H5F_ACC_RDWR);

    H5::DataSet dataset = file.openDataSet(buffer_name);
    H5::DataSpace dataspace = dataset.getSpace();
    int dimensions = dataspace.getSimpleExtentNdims();
    assert(dimensions <= 16);

    hsize_t dims[16];
    std::vector<int> dims_vec;
    assert( dataspace.getSimpleExtentDims(dims, NULL) == dimensions );
    for(int d=0; d<dimensions; d++) {
        dims_vec.insert(dims_vec.begin(), (int)dims[d]);
    }

    size_t mem_size = dataset.getInMemDataSize();
    ElemType *data = (ElemType *) malloc(mem_size);

    dataset.read(data, hdf5_type_from_halide_type(halide_type_of<ElemType>(), UseNative));

    BufferType retval = BufferType(BufferType::static_halide_type(), dims_vec);
    retval.allocate();
    fill_from_dense_buffer<BufferType>(retval, (void*)data);

    free(data);

    return retval;
}

}
}



#endif
