#include "PARDISOSolver.h"

#include <cassert>

#include "CompressedSparseMatrix.h"
#include "FloatMatrix.h"

template<>
PARDISOSolver< float, true >::PARDISOSolver()
{
	int options = MKL_DSS_DEFAULTS;
	options += MKL_DSS_SINGLE_PRECISION;
	options += MKL_DSS_ZERO_BASED_INDEXING;
	int retval = dss_create( m_handle, options );
	assert( retval == MKL_DSS_SUCCESS );
	(void)retval;
}

template<>
PARDISOSolver< float, false >::PARDISOSolver()
{
	int options = MKL_DSS_DEFAULTS;
	options += MKL_DSS_SINGLE_PRECISION;
	int retval = dss_create( m_handle, options );
	assert( retval == MKL_DSS_SUCCESS );
	(void)retval;
}

template<>
PARDISOSolver< double, true >::PARDISOSolver()
{
	int options = MKL_DSS_DEFAULTS;
	options += MKL_DSS_ZERO_BASED_INDEXING;
	int retval = dss_create( m_handle, options );
	assert( retval == MKL_DSS_SUCCESS );
	(void)retval;
}

template<>
PARDISOSolver< double, false >::PARDISOSolver()
{
	int options = MKL_DSS_DEFAULTS;
	int retval = dss_create( m_handle, options );
	assert( retval == MKL_DSS_SUCCESS );
	(void)retval;
}

// virtual
template< typename valueType, bool zeroBased >
PARDISOSolver< valueType, zeroBased >::~PARDISOSolver()
{
	int options = MKL_DSS_DEFAULTS;
	dss_delete( m_handle, options );
}

template< typename valueType, bool zeroBased >
bool PARDISOSolver< valueType, zeroBased >::analyzePattern( int m, int n, int* rowIndex, int* columns, int nNonZeroes )
{
	int structureOptions = MKL_DSS_SYMMETRIC;
	int retval = dss_define_structure( m_handle, structureOptions, rowIndex, m, n, columns, nNonZeroes );
	bool succeeded = ( retval == MKL_DSS_SUCCESS );
	assert( succeeded );

	if( succeeded )
	{
		int reorderOptions = MKL_DSS_DEFAULTS;
		//int reorderOptions = MKL_DSS_AUTO_ORDER;
		retval = dss_reorder( m_handle, reorderOptions, NULL );
		succeeded = ( retval == MKL_DSS_SUCCESS );
		assert( succeeded );
	}

	return succeeded;
}

template< typename valueType, bool zeroBased >
bool PARDISOSolver< valueType, zeroBased >::analyzePattern( CompressedSparseMatrix< valueType >& A )
{
	return analyzePattern( A.numRows(), A.numCols(),
		reinterpret_cast< int* >( A.outerIndexPointers().data() ),
		reinterpret_cast< int* >( A.innerIndices().data() ),
		A.numNonZeros() );
}

template< typename valueType, bool zeroBased >
bool PARDISOSolver< valueType, zeroBased >::factorize( valueType* values )
{
	//int options = MKL_DSS_INDEFINITE;
	int options = MKL_DSS_POSITIVE_DEFINITE;
	int retval = dss_factor_real( m_handle, options, values );
	bool succeeded = ( retval == MKL_DSS_SUCCESS );
	assert( succeeded );

	return succeeded;
}

template< typename valueType, bool zeroBased >
bool PARDISOSolver< valueType, zeroBased >::factorize( CompressedSparseMatrix< valueType >& A )
{
	assert( A.matrixType() == SYMMETRIC );

	return factorize( A.values().data() );
}

template< typename valueType, bool zeroBased >
bool PARDISOSolver< valueType, zeroBased >::solve( const valueType* rhs, valueType* solution )
{
	int options = MKL_DSS_DEFAULTS;
	int nRhs = 1;
	int retval = dss_solve_real( m_handle, options, rhs, nRhs, solution );
	bool succeeded = ( retval == MKL_DSS_SUCCESS );
	assert( succeeded );

	return succeeded;
}

template< typename valueType, bool zeroBased >
bool PARDISOSolver< valueType, zeroBased >::solve( const FloatMatrix& rhs, FloatMatrix& solution )
{
	return solve( rhs.constData(), solution.data() );
}

// instantiate

template
PARDISOSolver< float, true >::~PARDISOSolver();

template
PARDISOSolver< float, false >::~PARDISOSolver();

template
PARDISOSolver< double, true >::~PARDISOSolver();

template
PARDISOSolver< double, false >::~PARDISOSolver();

template
bool PARDISOSolver< float, true >::analyzePattern( int m, int n, int* rowIndex, int* columns, int nNonZeroes );

template
bool PARDISOSolver< float, false >::analyzePattern( int m, int n, int* rowIndex, int* columns, int nNonZeroes );

template
bool PARDISOSolver< double, true >::analyzePattern( int m, int n, int* rowIndex, int* columns, int nNonZeroes );

template
bool PARDISOSolver< double, false >::analyzePattern( int m, int n, int* rowIndex, int* columns, int nNonZeroes );

template
bool PARDISOSolver< float, true >::analyzePattern( CompressedSparseMatrix< float >& A );

template
bool PARDISOSolver< double, true >::analyzePattern( CompressedSparseMatrix< double >& A );

template
bool PARDISOSolver< float, true >::factorize( float* values );

template
bool PARDISOSolver< float, false >::factorize( float* values );

template
bool PARDISOSolver< double, true >::factorize( double* values );

template
bool PARDISOSolver< double, false >::factorize( double* values );

template
bool PARDISOSolver< float, true >::factorize( CompressedSparseMatrix< float >& A );

template
bool PARDISOSolver< double, true >::factorize( CompressedSparseMatrix< double >& A );

template
bool PARDISOSolver< float, true >::solve( const float* rhs, float* solution );

template
bool PARDISOSolver< float, false >::solve( const float* rhs, float* solution );

template
bool PARDISOSolver< double, true >::solve( const double* rhs, double* solution );

template
bool PARDISOSolver< double, false >::solve( const double* rhs, double* solution );

template
bool PARDISOSolver< float, true >::solve( const FloatMatrix& rhs, FloatMatrix& solution );
