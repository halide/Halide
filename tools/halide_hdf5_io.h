// This file provides a means to load/store Buffers from/to HDF5 multi-dimensional data files
// (see https://support.hdfgroup.org/HDF5/).

#ifndef HALIDE_HDF5_IO_H
#define HALIDE_HDF5_IO_H

#include "H5Cpp.h"
#include <vector>
#include <map>
#include <string>
#include <assert.h>

namespace Halide {
namespace Tools {

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

template<typename BufferType, typename ElemType>
ElemType * create_dense_buffer(BufferType &buf) {

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

    return retval;
}

template<typename BufferType, typename ElemType>
void fill_from_dense_buffer(BufferType &buf, ElemType *data) {
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
    copy_elems(data, buf.data(), buf.dimensions(), extents, src_strides, dst_strides);
}

template<typename BufferType, typename ElemType>
bool save_hdf5(std::vector<BufferType> buffers, std::vector<std::string> buffer_names, std::string filename) {

    assert(buffers.size() == buffer_names.size());

    //Open the HDF5 file.
    H5::H5File file(filename, H5F_ACC_TRUNC);

    size_t idx = 0;
    for(BufferType &b : buffers) {
        assert(b.dimensions() <= 16);

        hsize_t dims[16];
        for(int i=0; i<=b.dimensions(); i++){
            dims[i] = b.dim(b.dimensions()-1-i).extent();
        }

        H5::DataSpace dataspace(b.dimensions(), dims);
        H5::DataSet dataset = file.createDataSet(buffer_names[idx++], H5::PredType::STD_I32BE, dataspace);

        ElemType *data = create_dense_buffer<BufferType, ElemType>(b);
        dataset.write(data, H5::PredType::NATIVE_INT);
        free(data);
    }
}

template<typename BufferType, typename ElemType>
std::map<std::string, BufferType> load_from_hdf5(std::string filename, std::vector<std::string> buffer_names) {

    std::map<std::string, BufferType> retval;

    //Open the HDF5 file.
    H5::H5File file(filename, H5F_ACC_RDWR);

    for(std::string &s : buffer_names){
        H5::DataSet dataset = file.openDataSet(s);
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

        dataset.read(data, H5::PredType::NATIVE_INT);

        retval[s] = BufferType(BufferType::static_halide_type(), dims_vec);
        retval[s].allocate();
        fill_from_dense_buffer<BufferType, ElemType>(retval[s], data);

        free(data);
    }

    return retval;
}

}
}



#endif
