#include "CoordinateSparseMatrix.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

template< typename T >
CoordinateSparseMatrix< T >::CoordinateSparseMatrix() :

	m_nRows( 0 ),
	m_nCols( 0 )

{

}

template< typename T >
CoordinateSparseMatrix< T >::CoordinateSparseMatrix( uint initialCapacity ) :

	m_nRows( 0 ),
	m_nCols( 0 )

{
	m_ijv.reserve( initialCapacity );
}

template< typename T >
uint CoordinateSparseMatrix< T >::numNonZeroes() const
{
	return static_cast< uint >( m_ijv.size() );
}

template< typename T >
uint CoordinateSparseMatrix< T >::numRows() const
{
	return m_nRows;
}

template< typename T >
uint CoordinateSparseMatrix< T >::numCols() const
{
	return m_nCols;
}

template< typename T >
void CoordinateSparseMatrix< T >::append( uint i, uint j, const T& value )
{
	CoordinateSparseMatrix< T >::Triplet t;
	t.i = i;
	t.j = j;
	t.value = value;

	m_ijv.push_back( t );

	if( i >= m_nRows )
	{
		m_nRows = i + 1;
	}
	if( j >= m_nCols )
	{
		m_nCols = j + 1;
	}
}

template< typename T >
void CoordinateSparseMatrix< T >::clear()
{
	m_nRows = 0;
	m_nCols = 0;
	m_ijv.clear();
}

template< typename T >
void CoordinateSparseMatrix< T >::reserve( uint nnz )
{
	m_ijv.reserve( nnz );
}

template< typename T >
void CoordinateSparseMatrix< T >::compress( CompressedSparseMatrix< T >& output ) const
{
	uint m = numRows();
	uint n = numCols();
	uint nnz = numNonZeroes();
	output.reset( m, n, nnz );

	// copy the triplet vector
	std::vector< Triplet > ijvSorted( m_ijv );
	std::sort( ijvSorted.begin(), ijvSorted.end(), colMajorLess );
	compressCore( ijvSorted, output );

	// populate structure map
	auto& structureMap = output.structureMap();
	for( uint k = 0; k < nnz; ++k )
	{
		const Triplet& t = ijvSorted[k];
		uint i = t.i;
		uint j = t.j;
		structureMap[ std::make_pair( i, j ) ] = k;
	}
}

template< typename T >
void CoordinateSparseMatrix< T >::compressTranspose( CompressedSparseMatrix< T >& outputAt ) const
{
	uint m = numRows();
	uint n = numCols();
	uint nnz = numNonZeroes();
	outputAt.reset( n, m, nnz );

	// copy the triplet vector
	std::vector< Triplet > ijvSorted( m_ijv );

	// flip i and j
	for( uint k = 0; k < nnz; ++k )
	{
		Triplet& t = ijvSorted[k];
		std::swap( t.i, t.j );
	}

	std::sort( ijvSorted.begin(), ijvSorted.end(), colMajorLess );	

	compressCore( ijvSorted, outputAt );

	// populate structure map
	auto& structureMap = outputAt.structureMap();
	for( uint k = 0; k < nnz; ++k )
	{
		const Triplet& t = ijvSorted[k];
		// flip
		uint i = t.j;
		uint j = t.i;
		structureMap[ std::make_pair( i, j ) ] = k;
	}
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

template< typename T >
void CoordinateSparseMatrix< T >::compressCore( std::vector< Triplet > ijvSorted, CompressedSparseMatrix< T >& output ) const
{
	// TODO: if we want the output to be only symmetric or triangular
	//if( ( !upperTriangleOnly ) ||
	// ( j >= i ) )

	uint nnz = numNonZeroes();
	uint innerIndex = 0;
	uint outerIndexPointerIndex = 0;

	auto& values = output.values();
	auto& innerIndices = output.innerIndices();
	auto& outerIndexPointers = output.outerIndexPointers();

	for( uint k = 0; k < nnz; ++k )
	{
		const Triplet& t = ijvSorted[k];
		uint i = t.i;
		uint j = t.j;
		const T& value = t.value;

		values[ innerIndex ] = value;
		innerIndices[ innerIndex ] = i;

		if( j == outerIndexPointerIndex )
		{
			outerIndexPointers[ outerIndexPointerIndex ] = innerIndex;
			++outerIndexPointerIndex;
		}
		++innerIndex;
	}
	outerIndexPointers[ outerIndexPointerIndex ] = innerIndex;
	++outerIndexPointerIndex;	
}

// static
template< typename T >
bool CoordinateSparseMatrix< T >::rowMajorLess( Triplet& a, Triplet& b )
{
	if( a.i < b.i )
	{
		return true;
	}
	else if( a.i > b.i )
	{
		return false;
	}
	else
	{
		return( a.j < b.j );
	}
}

// static
template< typename T >
bool CoordinateSparseMatrix< T >::colMajorLess( Triplet& a, Triplet& b )
{
	if( a.j < b.j )
	{
		return true;
	}
	else if( a.j > b.j )
	{
		return false;
	}
	else
	{
		return( a.i < b.i );
	}
}

// //////////////////////////////////////////////////////////////////////////
// Instantiate Templates
//////////////////////////////////////////////////////////////////////////

template
CoordinateSparseMatrix< float >;

template
CoordinateSparseMatrix< double >;