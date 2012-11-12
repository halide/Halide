#if 0

#include "vecmath/GMatrixd.h"

#include <mkl.h>

#include <cassert>

GMatrixd::GMatrixd() :

m_nRows( 0 ),
m_nCols( 0 )
{

}

GMatrixd::GMatrixd( int nRows, int nCols, double fillValue ) :

m_nRows( nRows ),
m_nCols( nCols ),
m_data( nRows * nCols, fillValue )

{
	
}

GMatrixd::GMatrixd( const GMatrixd& m ) :

m_nRows( m.m_nRows ),
m_nCols( m.m_nCols ),
m_data( m.m_data )

{

}

// virtual
GMatrixd::~GMatrixd()
{

}

int GMatrixd::numRows() const
{
	return m_nRows;
}

int GMatrixd::numCols() const
{
	return m_nCols;
}

int GMatrixd::numElements() const
{
	return m_data.size();
}

void GMatrixd::indexToSubscript( int idx, int* pi, int* pj ) const
{
	*pi = idx / m_nRows;
	*pj = idx % m_nRows;
}

int GMatrixd::subscriptToIndex( int i, int j ) const
{
	return( j * m_nRows + i );
}

void GMatrixd::resize( int nRows, int nCols )
{
	if( nRows == m_nRows && nCols == m_nCols )
	{
		return;
	}

	m_nRows = nRows;
	m_nCols = nCols;
	m_data = QVector< double >( nRows * nCols, 0 );
}

void GMatrixd::reshape( int nRows, int nCols )
{
	// TODO: fix this
	assert( nRows * nCols == numElements() );

	m_nRows = nRows;
	m_nCols = nCols;
}

GMatrixd& GMatrixd::operator = ( const GMatrixd& m )
{
	if( &m != this )
	{
		m_nRows = m.m_nRows;
		m_nCols = m.m_nCols;
		m_data = m.m_data;
	}
	return *this;
}

bool GMatrixd::isNull() const
{
	return( m_nRows == 0 || m_nCols == 0 );
}

void GMatrixd::fill( double d )
{
	for( int i = 0; i < m_data.size(); ++i )
	{
		m_data[ i ] = d;
	}	
}

double& GMatrixd::operator () ( int i, int j )
{
	return( m_data[ j * m_nRows + i ] );
}

const double& GMatrixd::operator () ( int i, int j ) const
{
	return( m_data[ j * m_nRows + i ] );
}

double& GMatrixd::operator [] ( int k )
{
	return m_data[ k ];
}

const double& GMatrixd::operator [] ( int k ) const
{
	return m_data[ k ];
}

double* GMatrixd::data()
{
	return m_data.data();
}

void GMatrixd::transpose( GMatrixd& t )
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

void GMatrixd::eigenvalueDecomposition( QVector< QVector< double > >* eigen_vector,
									   QVector< double >* eigen_value )
{
	// TODO: assert matrix is square

	int N = m_nRows;
	double* gsl_proxy = new double[ N * N ];

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
void GMatrixd::times( const GMatrixd& a, const GMatrixd& b, GMatrixd& c )
{
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
void GMatrixd::homography( QVector< Vector3f > from,
						  QVector< Vector3f > to, GMatrixd& output )
{
	output.resize( 3, 3 );
	GMatrixd m( 8, 9 );

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

	QVector< QVector< double > > eigenvectors;
	QVector< double > eigenvalues;

	GMatrixd mt( 9, 8 );
	m.transpose( mt );

	GMatrixd mtm( 9, 9 );
	GMatrixd::times( mt, m, mtm );

	mtm.eigenvalueDecomposition( &eigenvectors, &eigenvalues );

	for( int i=0;i<3;i++){
		for( int j=0;j<3;j++){
			output( i, j ) = eigenvectors[0][3*i+j];
		}
	}
}

void GMatrixd::print()
{
	int M = numRows();
	int N = numCols();

	for( int i = 0; i < M; ++i )
	{
		for( int j = 0; j < N; ++j )
		{
			printf( "%.4lf    ", ( *this )( i, j ) );
		}
		printf( "\n" );
	}
}

#endif