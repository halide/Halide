#pragma once

#include <mkl_pardiso.h>

#include <common/BasicTypes.h>

template< typename valueType >
class CompressedSparseMatrix;

class FloatMatrix;

template< typename valueType, bool zeroBased >
class PARDISOSolver
{
public:

	PARDISOSolver();
	virtual ~PARDISOSolver();

	// Eigen's SparseMatrix:
	// Values <--> values
	// InnerIndices <--> columns
	// OuterIndexPtrs <--> rowIndex
	
	// analyzePattern: take in matrix sparsity structure
	// and perform fill-reducing ordering (symbolic factorization)
	bool analyzePattern( int m, int n, int* rowIndex, int* columns, int nNonZeroes );

	bool analyzePattern( CompressedSparseMatrix< valueType >& A );

	// factorize: take in the values, which has the same ordering as setup
	// if sparsity structure has changed, call setup again
	bool factorize( valueType* values );

	bool factorize( CompressedSparseMatrix< valueType >& A );

	// actually solve
	bool solve( const valueType* rhs, valueType* solution );

	bool solve( const FloatMatrix& rhs, FloatMatrix& solution );

private:

	_MKL_DSS_HANDLE_t m_handle;

};
