#include "LinearLeastSquaresSolvers.h"

#include <QtGlobal>
#include <mkl.h>

FloatMatrix LinearLeastSquaresSolvers::QRFullRank( const FloatMatrix& a, const FloatMatrix& b, bool* succeeded )
{
	FloatMatrix aa( a );
	FloatMatrix xx( b );

	bool bSucceeded = QRFullRankInPlace( aa, xx );
	if( succeeded != nullptr )
	{
		*succeeded = bSucceeded;
	}
	
	FloatMatrix x;
	if( bSucceeded )
	{
		x.resize( aa.numCols(), 1 );
		x.assign( xx, 0, 0, 0, 0, aa.numCols() );
	}
	return x;
}

bool LinearLeastSquaresSolvers::QRFullRank( const FloatMatrix& a, const FloatMatrix& b, FloatMatrix& x )
{
	FloatMatrix aa( a );
	FloatMatrix xx( b );
				
	bool succeeded = QRFullRankInPlace( aa, xx );
	if( succeeded )
	{
		x.resize( aa.numCols(), 1 );
		x.assign( xx, 0, 0, 0, 0, aa.numCols() );
	}
	return succeeded;
}

bool LinearLeastSquaresSolvers::QRFullRankInPlace( FloatMatrix& a, FloatMatrix& b )
{
	// TODO: check input sizes

	// call sgels(trans, m, n, nrhs, a, lda, b, ldb, work, lwork, info)

	char trans = 'N';
	int m = a.numRows();
	int n = a.numCols();
	int nrhs = b.numCols();
	int lda = m;
	int ldb = b.numRows();

	float* pa = a.data();
	float* pb = b.data();

	float workQuery;
	int lwork = -1;
	int info;

	// do work query
	sgels( &trans, &m, &n, &nrhs, pa, &lda, pb, &ldb, &workQuery, &lwork, &info );

	// TODO: check info

	lwork = static_cast< int >( workQuery );
	float* pwork = new float[ lwork ];

	sgels( &trans, &m, &n, &nrhs, pa, &lda, pb, &ldb, pwork, &lwork, &info );

	if( info < 0 )
	{
		fprintf( stderr, "QRFullRankInPlace: Illegal parameter value.\n" );
		return false;
	}
	if( info > 0 )
	{
		fprintf( stderr, "QRFullRankInPlace: Input matrix A is singular.\n" );
		return false;
	}
	return true;
}

// static
FloatMatrix LinearLeastSquaresSolvers::SVDRankDeficient( const FloatMatrix& a, const FloatMatrix& b, float rCond )
{
	FloatMatrix aa( a );
	FloatMatrix x( b );

	SVDRankDeficientInPlace( aa, x, rCond );
	return x;
}

// static
void LinearLeastSquaresSolvers::SVDRankDeficient( const FloatMatrix& a, const FloatMatrix& b, FloatMatrix& x, float rCond )
{
	FloatMatrix aa( a );
	x.copy( b );

	SVDRankDeficientInPlace( aa, x, rCond );
}

// static
void LinearLeastSquaresSolvers::SVDRankDeficientInPlace( FloatMatrix& a, FloatMatrix& b, float rCond )
{
	// TODO: check input size
				
	// call sgelss(m, n, nrhs, a, lda, b, ldb, s, rcond, rank, work, lwork, info)

	int m = a.numRows();
	int n = a.numCols();
	int nrhs = b.numCols();

	int lda = m;
	int ldb = b.numRows();
	
	float* pa = a.data();
	float* pb = b.data();

	// singular values
	float* ps = new float[ qMin( m, n ) ];

	int lRank;
	float workQuery;
	int lWork = -1;
	int info;

	// work query
	sgelss( &m, &n, &nrhs, pa, &lda, pb, &ldb, ps, &rCond, &lRank, &workQuery, &lWork, &info );
	// TODO: check info

	lWork = static_cast< int >( workQuery );
	float* pWork = new float[ lWork ];

	sgelss( &m, &n, &nrhs, pa, &lda, pb, &ldb, ps, &rCond, &lRank, pWork, &lWork, &info );
	// TODO: check info

	// TODO: return rank and singular values

	delete[] pWork;
	delete[] ps;
}