#include "SingularValueDecomposition.h"

#include <mkl.h>

// static
bool SingularValueDecomposition::SVD( FloatMatrix* a, FloatMatrix* u, FloatMatrix* s, FloatMatrix* vt )
{
	int m = a->numRows();
	int n = a->numCols();

	if( u->numRows() != m || u->numCols() != m ||
		s->numRows() != qMin( m, n ) ||
		vt->numRows() != n || vt->numRows() != n )
	{
		printf( "For m x n input a, u must be m x m, s must be min( m, n ) x 1, and vt must be n x n" );
		return false;
	}
				
	// make a copy of A
	std::shared_ptr< FloatMatrix > b( new FloatMatrix( a ) );
	float* pb = b->data();

	char jobu = 'A';
	char jobvt = 'A';

	int lda = m;
	int ldu = m;
	int ldvt = n;
				
	float* pu = u->data();
	float* pvt = vt->data();
	float* ps = s->data();

	float workQuery;
	int lwork = -1;
	int info;

	// do workspace query
	sgesvd( &jobu, &jobvt, &m, &n, pb, &lda, ps, pu, &ldu, pvt, &ldvt, &workQuery, &lwork, &info );

	lwork = static_cast< int >( workQuery );
	float* pwork = new float[ lwork ];	

	sgesvd( &jobu, &jobvt, &m, &n, pb, &lda, ps, pu, &ldu, pvt, &ldvt, pwork, &lwork, &info );

	delete[] pwork;

	// TODO: check info for error

	return true;
}

// static
std::shared_ptr< SingularValueDecomposition > SingularValueDecomposition::SVD( FloatMatrix* a )
{
	int m = a->numRows();
	int n = a->numCols();

	std::shared_ptr< FloatMatrix > u( new FloatMatrix( m, m ) );
	std::shared_ptr< FloatMatrix > s( new FloatMatrix( qMin( m, n ), 1 ) );
	std::shared_ptr< FloatMatrix > vt( new FloatMatrix( n, n ) );

	SingularValueDecomposition* pResult = nullptr;
	if( SVD( a, u.get(), s.get(), vt.get() ) )
	{
		pResult = new SingularValueDecomposition( u, s, vt );
	}
	
	return std::shared_ptr< SingularValueDecomposition >( pResult );
}

std::shared_ptr< FloatMatrix > SingularValueDecomposition::u()
{
	return m_u;
}

std::shared_ptr< FloatMatrix > SingularValueDecomposition::s()
{
	return m_s;
}

std::shared_ptr< FloatMatrix > SingularValueDecomposition::vt()
{
	return m_vt;
}

SingularValueDecomposition::SingularValueDecomposition( std::shared_ptr< FloatMatrix > u, std::shared_ptr< FloatMatrix > s, std::shared_ptr< FloatMatrix > vt ) :
	m_u( u ),
	m_s( s ),
	m_vt( vt )
{

}