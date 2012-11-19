#if 0

#include "vecmath/SparseMatrix.h"

#include <cassert>
#include <mkl_solver.h>

using namespace std;

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

SparseMatrix::SparseMatrix() :

m_nRows( 0 ),
m_nCols( 0 )

{

}

SparseMatrix::SparseMatrix( const SparseMatrix& other ) :

m_nRows( other.m_nRows ),
m_nCols( other.m_nCols ),
m_valueMap( other.m_valueMap )

{
	
}

SparseMatrix& SparseMatrix::operator = ( const SparseMatrix& other )
{
	if( this != &other )
	{
		m_nRows = other.m_nRows;
		m_nCols = other.m_nCols;
		m_valueMap = other.m_valueMap;
	}
	return *this;
}

void SparseMatrix::clear()
{
	m_nRows = 0;
	m_nCols = 0;
	m_valueMap.clear();
}

uint SparseMatrix::numRows()
{
	return m_nRows;
}

uint SparseMatrix::numCols()
{
	return m_nCols;
}

uint SparseMatrix::numNonZeroes()
{
	return m_valueMap.size();
}

double SparseMatrix::operator () ( uint i, uint j ) const
{
	SparseMatrixKey key( i, j );
	ConstIterator itr = m_valueMap.find( key );

	// find returns end() when it doesn't find anything
	if( itr != m_valueMap.end() )
	{
		return itr->second;
	}
	else
	{
		return 0;
	}
}

void SparseMatrix::put( uint i, uint j, double value )
{
	if( value != 0 )
	{
		if( i >= m_nRows )
		{
			m_nRows = i + 1;
		}
		if( j >= m_nCols )
		{
			m_nCols = j + 1;
		}

		m_valueMap[ SparseMatrixKey( i, j ) ] = value;
	}
}

SparseMatrix SparseMatrix::transposed()
{
	SparseMatrix mt;
	
	for( ConstIterator itr = m_valueMap.begin(); itr != m_valueMap.end(); ++itr )
	{
		SparseMatrixKey key = itr->first;
		double value = itr->second;

		mt.put( key.second, key.first, value );
	}

	mt.m_nRows = m_nCols;
	mt.m_nCols = m_nRows;

	return mt;
}

void SparseMatrix::print()
{
	printf( "size = (%d, %d)\n", m_nRows, m_nCols );
	printf( "===============\n" );
	for( ConstIterator itr = m_valueMap.begin(); itr != m_valueMap.end(); ++itr )
	{
		SparseMatrixKey key = itr->first;
		double value = itr->second;

		printf( "(%d,%d): %lf\n", key.first, key.second, value );
	}
}

SparseMatrix SparseMatrix::operator * ( const SparseMatrix& rhs )
{
	assert( m_nCols = rhs.m_nRows );

	SparseMatrix product;

	for( ConstIterator lhsItr = m_valueMap.begin(); lhsItr != m_valueMap.end(); ++lhsItr )
	{
		SparseMatrixKey lhsKey = lhsItr->first;
		double lhsValue = lhsItr->second;

		uint i = lhsKey.first;
		uint j = lhsKey.second;

		for( ConstIterator rhsItr = rhs.m_valueMap.begin(); rhsItr != rhs.m_valueMap.end(); ++rhsItr )
		{
			SparseMatrixKey rhsKey = rhsItr->first;
			double rhsValue = rhsItr->second;

			uint j2 = rhsKey.first;
			uint k = rhsKey.second;

			if( j == j2 )
			{
				product.put
				(
					i,
					k,
					product( i, k ) + lhsValue * rhsValue
				);
			}
		}
	}

	product.m_nRows = m_nRows;
	product.m_nCols = rhs.m_nCols;

	return product;
}

GMatrixd SparseMatrix::operator * ( const GMatrixd& rhs )
{
	uint nCols = rhs.numElements();
	assert( m_nCols == nCols );

	GMatrixd product( m_nRows, 1 );

	for( ConstIterator itr = m_valueMap.begin(); itr != m_valueMap.end(); ++itr )
	{
		uint i = itr->first.first;
		uint j = itr->first.second;
		double value = itr->second;

		product( i, j ) += value * rhs( j, 0 );
	}

	return product;
}

void SparseMatrix::directSolve( GMatrixd& b, SPARSE_MATRIX_STRUCTURE sms, GMatrixd* pOutput )
{
	// convert data from std::map<> into MKL row compressed storage
	int nnz = numNonZeroes();

	// TODO: convert these to DoubleArray, IntArray, etc

	DoubleArray values( nnz );
	IntArray columns( nnz );
	IntArray rowIndex( numRows() + 1 );

	uint valuesIndex = 0;
	uint columnsIndex = 0;
	uint rowIndexIndex = 0;

	for( ConstIterator itr = m_valueMap.begin(); itr != m_valueMap.end(); ++itr )
	{
		uint i = itr->first.first;
		uint j = itr->first.second;

		if( sms == SMS_STRUCTURALLY_SYMMETRIC ||
			sms == SMS_UNSYMMETRIC ||
			j >= i )
		{
			double value = itr->second;

			values[ valuesIndex ] = value;
			columns[ columnsIndex ] = j + 1;
			++valuesIndex;
			++columnsIndex;

			if( i == rowIndexIndex )
			{
				rowIndex[ rowIndexIndex ] = valuesIndex;
				++rowIndexIndex;
			}
		}		
	}
	rowIndex[ rowIndexIndex ] = valuesIndex + 1;
	++rowIndexIndex;

	// ===== Set up MKL =====

	// ----- input parameters -----
	int n = numCols();

	void* pt[ 64 ];
	for( int i = 0; i < 64; ++i )
	{
		pt[ i ] = 0;
	}

	int iparm[ 64 ];
	iparm[ 0 ] = 0; // use default values
	iparm[ 2 ] = 1; // use one processors, TODO: make this configurable

	int maxfct = 1; // always set to 1
	int mnum = 1; // always set to 1
	int mtype = int( sms );
	int phase = 13; // do full solve
	int* perm = NULL;
	int nrhs = 1; // number of right hand sides, always set to 1, TODO: set this to rhs.numCols()
	int msglvl = 0; // set to 1 to print stats	

	// ----- output parameters -----
	pOutput->reshape( n, 1 ); // solution
	int error = 0;
	
	// solve
	PARDISO( pt, &maxfct, &mnum, &mtype, &phase, &n, values, rowIndex, columns, perm, &nrhs, iparm, &msglvl, b.data(),
		pOutput->data(), &error );

	// release memory
	phase = -1;
	PARDISO( pt, &maxfct, &mnum, &mtype, &phase, &n, values, rowIndex, columns, perm, &nrhs, iparm, &msglvl, b.data(),
		pOutput->data(), &error );
}

#if 0
DoubleArray SparseMatrix::solveConjugateGradient( DoubleArray b )
{
	int maxNumIterations = 150; // should be min( n, input param)

	// input parameters
	int n = b.length();
	DoubleArray x( n, 0 );

	// output parameters
	int rciRequest;
	int ipar[ 128 ];
	double dpar[ 128 ];
	DoubleArray tmp( n * 4, 0 );

	DCG_INIT
	(
		&n, // size
		x.data(), // initial guess
		b.data(), // rhs,
		&rciRequest,
		ipar,
		dpar,
		tmp.data()
	);

	// check that initialization is ok
	assert( rciRequest == 0 );

#if _DEBUG
	// turn off warnings and errors
	ipar[ 2 ] = 0; // output for errors and warnings
	ipar[ 5 ] = 0; // turn off errors
	ipar[ 6 ] = 0; // turn off warnings

#endif

	ipar[ 4 ] = maxNumIterations;

	// ipar[ 11 ]: 0 --> no preconditioner, !0 --> precondition

	// dpar[ 0 ] = relative tolerance, default = 1e-6
	// dpar[ 1 ] = absolute tolerance, default = 0
#if 1
	DCG_CHECK
	(
		&n, // size
		x.data(), // initial guess
		b.data(), // rhs,
		&rciRequest,
		ipar,
		dpar,
		tmp.data()
	);
#endif

	assert( rciRequest == 0 );

	return x;
}
#endif

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

// static
const uint SparseMatrix::DEFAULT_NNZ = 1000;


#endif