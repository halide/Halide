#if 0

#ifndef SPARSE_MATRIX_H
#define SPARSE_MATRIX_H

#include <map>

#include "common/BasicTypes.h"
#include "vecmath/MatrixT.h"
#include "vecmath/GMatrixd.h"

class SparseMatrix
{
	typedef std::pair< unsigned int, unsigned int > SparseMatrixKey;
    typedef std::map< SparseMatrixKey, double > SparseMatrixMap;
    typedef SparseMatrixMap::iterator Iterator;
    typedef SparseMatrixMap::const_iterator ConstIterator;

public:

	enum SPARSE_MATRIX_STRUCTURE
	{
		SMS_STRUCTURALLY_SYMMETRIC = 1,
		SMS_SYMMETRIC_INDEFINITE = -2,
		SMS_SYMMETRIC_POSITIVE_DEFINITE = 2,
		SMS_UNSYMMETRIC = 11
	};

	SparseMatrix();
	SparseMatrix( const SparseMatrix& other );
	SparseMatrix& operator = ( const SparseMatrix& other );
	
	void clear(); // resets this to a 0x0 empty sparse matrix

	uint numRows();
	uint numCols();
	uint numNonZeroes();

	double operator () ( uint i, uint j ) const;
	void put( uint i, uint j, double value );	

	SparseMatrix transposed();

	// Sparse * Sparse
	SparseMatrix operator * ( const SparseMatrix& rhs );
	
	// Sparse * Dense Vector
	GMatrixd operator * ( const GMatrixd& rhs );

	// Direct solve using MKL's PARDISO
	void directSolve( GMatrixd& b, SPARSE_MATRIX_STRUCTURE sms, GMatrixd* pOutput );

	// DoubleArray SparseMatrix::solveConjugateGradient( DoubleArray b );

	void print();

private:

	static const uint DEFAULT_NNZ;

	// maintained as the maximum inserted value
	uint m_nRows;
	uint m_nCols;

	// hash table of actual values
	SparseMatrixMap m_valueMap;
};

#endif // SPARSE_MATRIX_T_H

#endif