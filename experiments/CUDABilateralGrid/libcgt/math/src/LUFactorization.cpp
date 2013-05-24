#include "LUFactorization.h"

#include <mkl.h>
#include <QtGlobal>

// static
std::shared_ptr< LUFactorization > LUFactorization::LU( const FloatMatrix& a )
{
	int m = a.numRows();
	int n = a.numCols();

	std::shared_ptr< LUFactorization > result( nullptr );
	LUFactorization* plu = new LUFactorization( a );

	float* py = plu->m_y.data();
	int* pipiv = &( plu->m_ipiv[0] );
	int info;
				
	sgetrf( &m, &n, py, &m, pipiv, &info );
	if( info < 0 )
	{
		fprintf( stderr, "LUFactorization::LU: Illegal parameter value.\n" );		
	}
	else
	{
		result.reset( plu );
	}

	return result;
}

bool LUFactorization::inverse( FloatMatrix& output )
{
	int m = m_nRowsA;
	int n = m_nColsA;

	output.copy( m_y );
	float* pa = output.data();
	int* pipiv = &( m_ipiv[ 0 ] );

	// ask for how much work space is needed		
	float workQuery;
	int lwork = -1;
	int info;

	sgetri( &n, pa, &m, pipiv, &workQuery, &lwork, &info );
	if( info < 0 )
	{
		fprintf( stderr, "LUFactorization::inverse(): Illegal parameter value.\n" );
		return false;
	}

	lwork = static_cast< int >( workQuery );
	float* pwork = new float[ lwork ];

	// solve
	sgetri( &n, pa, &m, pipiv, pwork, &lwork, &info );
	if( info < 0 )
	{
		fprintf( stderr, "LUFactorization::inverse(): Illegal parameter value.\n" );
		return false;
	}		
	if( info > 0 )
	{
		fprintf( stderr, "LUFactorization::inverse(): A is singular.\n" );
		return false;
	}
	return true;
}

#if 0
			FloatMatrix^ LUFactorization::L::get()
			{
				if( m_l == nullptr )
				{
					int k = Math::Max( m_nRowsA, m_nColsA );

					FloatMatrix^ tmp = FloatMatrix::Identity( k );
					FloatMatrix::CopyLowerTriangleExclusive( m_y, tmp );
					
					m_l = gcnew FloatMatrix( k, k );
					
					// swap rows
					for( int idx = 0; idx < m_ipiv->Length; ++idx )
					{
						int i = m_ipiv[ idx ] - 1;

						for( int j = 0; j < k; ++j )
						{
							m_l[ idx, j ] = tmp[ i, j ];
						}
					}
					
				}

				return m_l;
			}

			FloatMatrix^ LUFactorization::U::get()
			{
				if( m_u == nullptr )
				{
					int k = Math::Min( m_nRowsA, m_nColsA );
					m_u = gcnew FloatMatrix( k, k );
					FloatMatrix::CopyUpperTriangle( m_y, m_u );
				}

				return m_u;
			}

			
#endif

LUFactorization::LUFactorization( const FloatMatrix& a ) :

	m_nRowsA( a.numRows() ),
	m_nColsA( a.numCols() ),

	m_y( a ),
	m_ipiv( qMin( a.numRows(), a.numCols() ) )

{

}
