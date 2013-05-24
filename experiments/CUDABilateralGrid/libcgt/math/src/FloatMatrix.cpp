#include "FloatMatrix.h"

#include <mkl.h>
#include <mkl_blas.h>

#include <cassert>
#include <cstdarg>

#include <algorithm>

#include "LUFactorization.h"

// static
FloatMatrix FloatMatrix::zeroes( int m, int n )
{
	return FloatMatrix( m, n );
}

// static
FloatMatrix FloatMatrix::ones( int m, int n )
{
	return FloatMatrix( m, n, 1.0f );
}

FloatMatrix::FloatMatrix() :

	m_nRows( 0 ),
	m_nCols( 0 )

{

}

FloatMatrix::FloatMatrix( int nRows, int nCols, float fillValue ) :

	m_nRows( nRows ),
	m_nCols( nCols ),
	m_data( nRows * nCols, fillValue )

{

}

FloatMatrix::FloatMatrix( const FloatMatrix& m ) :

	m_nRows( m.m_nRows ),
	m_nCols( m.m_nCols ),
	m_data( m.m_data )

{

}

FloatMatrix::FloatMatrix( FloatMatrix&& m )
{
	*this = std::move( m );
}

FloatMatrix::FloatMatrix( FloatMatrix* m ) :

	m_nRows( m->m_nRows ),
	m_nCols( m->m_nCols ),
	m_data( m->m_data )

{

}

// virtual
FloatMatrix::~FloatMatrix()
{

}

FloatMatrix& FloatMatrix::operator = ( const FloatMatrix& m )
{
	if( this != &m )
	{
		m_nRows = m.m_nRows;
		m_nCols = m.m_nCols;
		m_data = m.m_data;
	}
	return *this;
}

FloatMatrix& FloatMatrix::operator = ( FloatMatrix&& m )
{
	if( this != &m )
	{
		m_nRows = m.m_nRows;
		m_nCols = m.m_nCols;
		m_data = std::move( m.m_data );
	}
	return *this;
}

int FloatMatrix::numRows() const
{
	return m_nRows;
}

int FloatMatrix::numCols() const
{
	return m_nCols;
}

int FloatMatrix::numElements() const
{
	return m_data.size();
}

Vector2i FloatMatrix::indexToSubscript( int idx ) const
{
	return Vector2i( idx / m_nRows, idx % m_nRows );
}

int FloatMatrix::subscriptToIndex( int i, int j ) const
{
	return( j * m_nRows + i );
}

void FloatMatrix::resize( int nRows, int nCols )
{
	if( nRows == m_nRows && nCols == m_nCols )
	{
		return;
	}

	m_nRows = nRows;
	m_nCols = nCols;
	m_data.resize( nRows * nCols, 0 );
}

bool FloatMatrix::reshape( int nRows, int nCols )
{
	if( nRows * nCols != numElements() )
	{
		return false;
	}

	m_nRows = nRows;
	m_nCols = nCols;
	return true;
}

bool FloatMatrix::isNull() const
{
	return( m_nRows == 0 || m_nCols == 0 );
}

void FloatMatrix::fill( float d )
{
	for( int i = 0; i < m_data.size(); ++i )
	{
		m_data[ i ] = d;
	}	
}

float& FloatMatrix::operator () ( int i, int j )
{
	return( m_data[ j * m_nRows + i ] );
}

const float& FloatMatrix::operator () ( int i, int j ) const
{
	return( m_data[ j * m_nRows + i ] );
}

float& FloatMatrix::operator [] ( int k )
{
	return m_data[ k ];
}

const float& FloatMatrix::operator [] ( int k ) const
{
	return m_data[ k ];
}

void FloatMatrix::copy( const FloatMatrix& m )
{
	resize( m.m_nRows, m.m_nCols );
	m_data = m.m_data;
}

void FloatMatrix::assign( const FloatMatrix& m, int i0, int j0, int i1, int j1, int nRows, int nCols )
{
	assert( i0 >= 0 );
	assert( i0 < numRows() );
	assert( j0 >= 0 );
	assert( j0 < numCols() );

	assert( i1 >= 0 );
	assert( i1 < m.numRows() );
	assert( j1 >= 0 );
	assert( j1 < m.numCols() );

	if( nRows == 0 )
	{
		nRows = m.numRows() - i1;
	}
	if( nCols == 0 )
	{
		nCols = m.numCols() - j1;
	}	

	assert( i0 + nRows <= numRows() );
	assert( j0 + nCols <= numCols() );
	assert( i1 + nRows <= m.numRows() );
	assert( j1 + nCols <= m.numCols() );

	for( int i = 0; i < nRows; ++i )
	{
		for( int j = 0; j < nCols; ++j )
		{
			(*this)( i0 + i, j0 + j ) = m( i1 + i, j1 + j );
		}
	}
}

float* FloatMatrix::data()
{
	return m_data.data();
}

const float* FloatMatrix::constData() const
{
	return m_data.data();
}

FloatMatrix FloatMatrix::inverted() const
{
	FloatMatrix inv;
	std::shared_ptr< LUFactorization > lu = LUFactorization::LU( *this );
	lu->inverse( inv );

	return inv;
}

void FloatMatrix::inverse( FloatMatrix& inv ) const
{
	std::shared_ptr< LUFactorization > lu = LUFactorization::LU( *this );
	lu->inverse( inv );
}

void FloatMatrix::transpose( FloatMatrix& t ) const
{
	int M = m_nRows;
	int N = m_nCols;

	t.resize( N, M );

	for( int i = 0; i < M; ++i )
	{
		for( int j = 0; j < N; ++j )
		{
			t( j, i ) = ( *this )( i, j );
		}
	}
}

FloatMatrix FloatMatrix::transposed() const
{
	FloatMatrix t( m_nCols, m_nRows );
	transpose( t );
	return t;
}

FloatMatrix& FloatMatrix::operator += ( const FloatMatrix& x )
{
	scaledMultiplyAdd( 1.0f, x, *this );
	return( *this );
}

FloatMatrix& FloatMatrix::operator -= ( const FloatMatrix& x )
{
	scaledMultiplyAdd( -1.0f, x, *this );
	return( *this );
}

// static
float FloatMatrix::dot( const FloatMatrix& a, const FloatMatrix& b )
{
	assert( a.numRows() == 1 || a.numCols() == 1 );
	assert( b.numRows() == 1 || b.numCols() == 1 );
	assert( a.numElements() == b.numElements() );

	int m = a.numElements();
	int inc = 1;
	return sdot( &m, a.constData(), &inc, b.constData(), &inc );
}

// static
void FloatMatrix::add( const FloatMatrix& a, const FloatMatrix& b, FloatMatrix& c )
{
	assert( a.numRows() == b.numRows() );
	assert( a.numCols() == b.numCols() );

	c.resize( a.numRows(), a.numCols() );

	for( int k = 0; k < a.numElements(); ++k )
	{
		c[k] = a[k] + b[k];
	}
}

// static
void FloatMatrix::subtract( const FloatMatrix& a, const FloatMatrix& b, FloatMatrix& c )
{
	assert( a.numRows() == b.numRows() );
	assert( a.numCols() == b.numCols() );

	c.resize( a.numRows(), a.numCols() );

	for( int k = 0; k < a.numElements(); ++k )
	{
		c[k] = a[k] - b[k];
	}
}

// static
void FloatMatrix::multiply( const FloatMatrix& a, const FloatMatrix& b, FloatMatrix& c )
{
	assert( a.numCols() == b.numRows() );

	c.resize( a.numRows(), b.numCols() );

	for( int i = 0; i < a.numRows(); ++i )
	{
		for( int k = 0; k < b.numCols(); ++k )
		{
			c( i, k ) = 0;
		}
	}

	for( int i = 0; i < a.numRows(); ++i )
	{
		for( int j = 0; j < a.numCols(); ++j )
		{
			for( int k = 0; k < b.numCols(); ++k )
			{
				c( i, k ) += a( i, j ) * b( j, k );
			}
		}
	}
}

// static
void FloatMatrix::scaledMultiplyAdd( float alpha, const FloatMatrix& x, FloatMatrix& y )
{	
	int n = x.numElements();
	assert( n == y.numElements() );
	int inc = 1;

	saxpy( &n, &alpha, x.constData(), &inc, y.data(), &inc );
}

#if 0
void FloatMatrix::eigenvalueDecomposition( QVector< QVector< float > >* eigen_vector,
									   QVector< float >* eigen_value )
{
	// TODO: assert matrix is square

	int N = m_nRows;
	float* gsl_proxy = new float[ N * N ];

	eigen_vector->resize(N);
	for( int i = 0; i < N; ++i )
	{
		( *eigen_vector )[ i ].resize( N );
	}

	eigen_value->resize(N);

	for( int i=0;i<N;i++){
		for( int j=0;j<N;j++){
			gsl_proxy[i*N+j] = ( *this )( i, j );
		}
	}

	// TODO: don't alloc, etc
	// TODO: MKL?
	gsl_matrix_view gsl_mat      = gsl_matrix_view_array(gsl_proxy,N,N);
	gsl_vector* gsl_eigen_value  = gsl_vector_alloc(N);
	gsl_matrix* gsl_eigen_vector = gsl_matrix_alloc(N,N);
	gsl_eigen_symmv_workspace* w = gsl_eigen_symmv_alloc(N);

	gsl_eigen_symmv(&gsl_mat.matrix,gsl_eigen_value,gsl_eigen_vector,w);
	gsl_eigen_symmv_sort(gsl_eigen_value,gsl_eigen_vector,GSL_EIGEN_SORT_ABS_ASC);

	for( int i=0;i<N;i++){

		(*eigen_value)[i] = gsl_vector_get(gsl_eigen_value,i);

		for( int j=0;j<N;j++){
			(*eigen_vector)[i][j] = gsl_matrix_get(gsl_eigen_vector,j,i);
		}
	}

	gsl_matrix_free(gsl_eigen_vector);
	gsl_vector_free(gsl_eigen_value);
	gsl_eigen_symmv_free(w);   
	delete[] gsl_proxy;
}

// static
void FloatMatrix::homography( QVector< Vector3f > from,
						  QVector< Vector3f > to, FloatMatrix& output )
{
	output.resize( 3, 3 );
	FloatMatrix m( 8, 9 );

	for( int i = 0; i < 4; ++i )
	{
		m(2*i,0) = from[i].x() * to[i].z();
		m(2*i,1) = from[i].y() * to[i].z();
		m(2*i,2) = from[i].z() * to[i].z();
		m(2*i,6) = -from[i].x() * to[i].x();
		m(2*i,7) = -from[i].y() * to[i].x();
		m(2*i,8) = -from[i].z() * to[i].x();

		m(2*i+1,3) = from[i].x() * to[i].z();
		m(2*i+1,4) = from[i].y() * to[i].z();
		m(2*i+1,5) = from[i].z() * to[i].z();
		m(2*i+1,6) = -from[i].x() * to[i].y();
		m(2*i+1,7) = -from[i].y() * to[i].y();
		m(2*i+1,8) = -from[i].z() * to[i].y();
	}

	QVector< QVector< float > > eigenvectors;
	QVector< float > eigenvalues;

	FloatMatrix mt( 9, 8 );
	m.transpose( mt );

	FloatMatrix mtm( 9, 9 );
	FloatMatrix::times( mt, m, mtm );

	mtm.eigenvalueDecomposition( &eigenvectors, &eigenvalues );

	for( int i=0;i<3;i++){
		for( int j=0;j<3;j++){
			output( i, j ) = eigenvectors[0][3*i+j];
		}
	}
}
#endif

float FloatMatrix::minimum() const
{
	return *( std::min_element( m_data.begin(), m_data.end() ) );
}

float FloatMatrix::maximum() const
{
	return *( std::max_element( m_data.begin(), m_data.end() ) );
}

void FloatMatrix::print( const char* prefix, const char* suffix )
{
	if( prefix != nullptr )
	{
		printf( "%s\n", prefix );
	}

	int M = numRows();
	int N = numCols();

	for( int i = 0; i < M; ++i )
	{
		for( int j = 0; j < N; ++j )
		{
			printf( "%.4f    ", ( *this )( i, j ) );
		}
		printf( "\n" );
	}
	printf( "\n" );

	if( suffix != nullptr )
	{
		printf( "%s\n", suffix );
	}
}

QString FloatMatrix::toString()
{
	int M = numRows();
	int N = numCols();

	QString out;

	for( int i = 0; i < M; ++i )
	{
		for( int j = 0; j < N; ++j )
		{
			float val = ( *this )( i, j );
			out.append( QString( "%1" ).arg( val, 10, 'g', 4 ) );
		}
		out.append( "\n" );
	}
	return out;
}

FloatMatrix operator + ( const FloatMatrix& a, const FloatMatrix& b )
{
	FloatMatrix c;
	FloatMatrix::add( a, b, c );
	return c;
}

FloatMatrix operator - ( const FloatMatrix& a, const FloatMatrix& b )
{
	FloatMatrix c;
	FloatMatrix::subtract( a, b, c );
	return c;
}

FloatMatrix operator - ( const FloatMatrix& a )
{
	// TODO: multiply( -1, a )
	FloatMatrix c( a );
	for( int k = 0; k < c.numElements(); ++k )
	{
		c[k] = -c[k];
	}
	return c;
}

FloatMatrix operator * ( const FloatMatrix& a, const FloatMatrix& b )
{
	FloatMatrix c( a.numRows(), b.numCols() );
	FloatMatrix::multiply( a, b, c );
	return c;
}
