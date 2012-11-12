#include "vecmath/MatrixT.h"

#if 0

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cfloat>

#include "../io/BinaryFileInputStream.h"
#include "../math/MathUtils.h"

// TODO: fix this
#ifndef isinf
#define isinf(x) (!(_finite(x)))
#endif

using namespace std;

// ==============================================================
// Public
// ==============================================================

// static
bool GMatrixf::loadFromBinaryFile( const char* filename, vector< GMatrixf* >* pvMatrices )
{
	pvMatrices->clear();
	
	BinaryFileInputStream* pStream = BinaryFileInputStream::open( filename );
	bool succeeded = ( pStream != NULL );
	if( succeeded )
	{
		int numMatrices;
		int size[2];
		pStream->readInt( &numMatrices );

		for( int i = 0; i < numMatrices; ++i )
		{
			pStream->readIntArray( size, 2 );
			pvMatrices->push_back( new GMatrixf( size[0], size[1] ) );
		}

		for( int i = 0; i < numMatrices; ++i )
		{
			GMatrixf* pMatrix = pvMatrices->at( i );

			int N = ( pMatrix->getNumRows() ) * ( pMatrix->getNumCols() );
			float* afElements = pMatrix->m_afElements;

			pStream->readFloatArray( afElements, N );
		}
		
		delete pStream;
	}

	return succeeded;
}

GMatrixf::GMatrixf( int iRows, int iCols ) :
	m_iRows( iRows ),
	m_iCols( iCols )
{
	m_afElements = new float[ iRows * iCols ];

	for( int i = 0; i < iRows * iCols; ++i )
	{
		m_afElements[i] = 0;
	}
}

GMatrixf::GMatrixf( int iRows, int iCols, float fValue ) :
	m_iRows( iRows ),
	m_iCols( iCols )
{
	m_afElements = new float[ iRows * iCols ];

	for( int i = 0; i < iRows * iCols; ++i )
	{
		m_afElements[i] = fValue;
	}
}

// pointer copy constructor
GMatrixf::GMatrixf( GMatrixf* pCopy ) :
	m_iRows( pCopy->m_iRows ),
	m_iCols( pCopy->m_iCols )
{
	m_afElements = new float[ m_iRows * m_iCols ];

	for( int i = 0; i < m_iRows * m_iCols; ++i )
	{
		m_afElements[i] = pCopy->m_afElements[i];
	}
}

// virtual
GMatrixf::~GMatrixf()
{
	delete[] m_afElements;
}

const float& GMatrixf::operator () ( int i, int j ) const
{
	// return m_afElements[ iRowIndex * m_iCols + iColIndex ]; // row major order
	return m_afElements[ j * m_iRows + i ]; // col major order
}

float& GMatrixf::operator () ( int i, int j )
{
	// return m_afElements[ iRowIndex * m_iCols + iColIndex ]; // row major order
	return m_afElements[ j * m_iRows + i ]; // col major order
}

// I/O
bool GMatrixf::printToTextFile( const char* filename )
{
	bool succeeded = false;
	FILE* fp = fopen( filename, "w" );
	if( fp != NULL )
	{
		succeeded = true;
		for( int i = 0; i < m_iRows; ++i )
		{
			fprintf( fp, "[ " );
			for( int j = 0; j < m_iCols; ++j )
			{
				fprintf( fp, "%.2f ", get( i, j ) );
			}
			fprintf( fp, "]\n" );
		}
		fclose( fp );
	}

	return succeeded;
}

// Accessors

void GMatrixf::setZero()
{
	for( int i = 0; i < m_iRows * m_iCols; ++i )
	{
		m_afElements[i] = 0;
	}
}

float GMatrixf::get( int iRowIndex, int iColIndex )
{
	// return m_afElements[ iRowIndex * m_iCols + iColIndex ]; // row major order
	return m_afElements[ iColIndex * m_iRows + iRowIndex ]; // col major order
}

void GMatrixf::set( int iRowIndex, int iColIndex, float fValue )
{
	// m_afElements[ iRowIndex * m_iCols + iColIndex ] = fValue ;
	m_afElements[ iColIndex * m_iRows + iRowIndex ] = fValue; // col major order
}

bool GMatrixf::set( GMatrixf* pm )
{
	if( pm == NULL )
	{
		return false;
	}

	if( ( m_iRows != pm->getNumRows() ) ||
		( m_iCols != pm->getNumCols() ) )
	{
		return false;
	}

	for( int i = 0; i < m_iRows; ++i )
	{
		for( int j = 0; j < m_iCols; ++j )
		{
			set( i, j, pm->get( i, j ) );
		}
	}

	return true;
}

void GMatrixf::getRow( int iRowIndex, GMatrixf* pRow )
{
	for( int i = 0; i < m_iCols; ++i )
	{
		float fValue = get( iRowIndex, i );
		pRow->set( 0, i, fValue );
	}
}

void GMatrixf::setRow( int iRowIndex, GMatrixf* pRow )
{
	for( int i = 0; i < m_iCols; ++i )
	{
		set( iRowIndex, i, pRow->get( 0, i ) );
	}
}

void GMatrixf::getCol( int iColIndex, GMatrixf* pCol )
{
	for( int i = 0; i < m_iRows; ++i )
	{
		float fValue = get( i, iColIndex );
		pCol->set( i, 0, fValue );
	}
}

void GMatrixf::setCol( int iColIndex, GMatrixf* pCol )
{
	for( int i = 0; i < m_iRows; ++i )
	{
		set( i, iColIndex, pCol->get( i, 0 ) );
	}
}

int GMatrixf::getNumRows()
{
	return m_iRows;
}

int GMatrixf::getNumCols()
{
	return m_iCols;
}

void GMatrixf::print()
{
	for( int i = 0; i < m_iRows; ++i )
	{
		printf( "[ " );
		for( int j = 0; j < m_iCols; ++j )
		{
			printf( "%.2f ", get( i, j ) );
		}
		printf( "]\n" );
	}
}

QString GMatrixf::toString()
{
	QString str;

	for( int i = 0; i < m_iRows; ++i )
	{
		str += "[ ";
		for( int j = 0; j < m_iCols; ++j )
		{
			str += QString( "%1 " ).arg( get( i, j ), 4 );
		}
		str += "]\n";
	}

	return str;
}

// Math

void GMatrixf::multiply( float f )
{
	for( int i = 0; i < m_iRows * m_iCols; ++i )
	{
		m_afElements[i] *= f;
	}
}

bool GMatrixf::transpose( GMatrixf* pm, GMatrixf* pmOut )
{
	if( ( pm == NULL ) ||
		( pmOut == NULL ) )
	{
		return false;
	}

	if( ( pm->m_iRows != pmOut->m_iCols ) ||
		( pm->m_iCols != pmOut->m_iRows ) )
	{
		return false;
	}

	if( pm != pmOut )
	{
		for( int i = 0; i < pm->m_iRows; ++i )
		{
			for( int j = 0; j < pm->m_iCols; ++j )
			{
				pmOut->set( j, i, pm->get( i, j ) );
			}
		}
	}
	else
	{
		float fValue;
		for( int i = 0; i < pm->m_iRows; ++i )
		{
			for( int j = i + 1; j < pm->m_iCols; ++j )
			{
				fValue = pm->get( i, j );
				pm->set( i, j, pm->get( j, i ) );
				pm->set( j, i, fValue );
			}
		}
	}

	return true;
}

// Static

bool GMatrixf::add( GMatrixf* pmLHS, GMatrixf* pmRHS, GMatrixf* pmOut )
{
	if( ( pmLHS == NULL ) ||
		( pmRHS == NULL ) ||
		( pmOut == NULL ) )
	{
		return false;
	}

	if( ( pmLHS->getNumRows() != pmRHS->getNumRows() ) ||
		( pmLHS->getNumRows() != pmOut->getNumRows() ) ||
		( pmLHS->getNumCols() != pmRHS->getNumCols() ) ||
		( pmLHS->getNumCols() != pmOut->getNumCols() ) )
	{
		return false;
	}

	for( int i = 0; i < pmLHS->getNumRows(); ++i )
	{
		for( int j = 0; j < pmRHS->getNumCols(); ++j )
		{
			pmOut->set( i, j, pmLHS->get( i, j ) + pmRHS->get( i, j ) );
		}
	}

	return true;
}

bool GMatrixf::subtract( GMatrixf* pmLHS, GMatrixf* pmRHS, GMatrixf* pmOut )
{
	if( ( pmLHS == NULL ) ||
		( pmRHS == NULL ) ||
		( pmOut == NULL ) )
	{
		return false;
	}

	if( ( pmLHS->getNumRows() != pmRHS->getNumRows() ) ||
		( pmLHS->getNumRows() != pmOut->getNumRows() ) ||
		( pmLHS->getNumCols() != pmRHS->getNumCols() ) ||
		( pmLHS->getNumCols() != pmOut->getNumCols() ) )
	{
		return false;
	}

	for( int i = 0; i < pmLHS->getNumRows(); ++i )
	{
		for( int j = 0; j < pmRHS->getNumCols(); ++j )
		{
			pmOut->set( i, j, pmLHS->get( i, j ) - pmRHS->get( i, j ) );
		}
	}

	return true;
}

bool GMatrixf::multiply( float f, GMatrixf* pm, GMatrixf* pmOut )
{
	if( pm == NULL )
	{
		return false;
	}

	if( pm == pmOut )
	{
		pm->multiply( f );
	}
	else
	{
		for( int i = 0; i < pm->getNumRows(); ++i )
		{
			for( int j = 0; j < pm->getNumCols(); ++j )
			{
				pmOut->set( i, j, f * pm->get( i, j ) );
			}
		}
	}

	return true;
}

bool GMatrixf::multiply( GMatrixf* pmLHS, GMatrixf* pmRHS, GMatrixf* pmOut )
{
	if( ( pmLHS == NULL ) ||
		( pmRHS == NULL ) ||
		( pmOut == NULL ) )
	{
		return false;
	}

	if( ( pmLHS->getNumCols() != pmRHS->getNumRows() ) ||
		( pmLHS->getNumRows() != pmOut->getNumRows() ) ||
		( pmRHS->getNumCols() != pmOut->getNumCols() ) )
	{
		return false;
	}

	if( ( pmLHS != pmOut ) && ( pmRHS != pmOut ) )
	{
		pmOut->setZero();

		float fValue;
		for( int i = 0; i < pmLHS->getNumRows(); ++i )
		{
			for( int j = 0; j < pmLHS->getNumCols(); ++j )
			{
				for( int k = 0; k < pmRHS->getNumCols(); ++k )
				{
					fValue = pmOut->get( i, k ) + pmLHS->get( i, j ) * pmRHS->get( j, k );
					pmOut->set( i, k, fValue );
				}
			}
		}
	}
	else
	{
		GMatrixf product( pmLHS->getNumRows(), pmRHS->getNumCols() );
		product.setZero();

		float fValue;
		for( int i = 0; i < pmLHS->getNumRows(); ++i )
		{
			for( int j = 0; j < pmLHS->getNumCols(); ++j )
			{
				for( int k = 0; k < pmRHS->getNumCols(); ++k )
				{
					fValue = product.get( i, k ) + pmLHS->get( i, j ) * pmRHS->get( j, k );
					product.set( i, k, fValue );
				}
			}
		}

		pmOut->set( &product );
	}

	return true;
}

// static
void GMatrixf::getVectorFromMatrixStack( vector< GMatrixf* >* pvMatrixStack,
										int i, int j,
										GMatrixf* pmOutputVector )
{
	for( uint k = 0; k < pvMatrixStack->size(); ++k )
	{
		float value = pvMatrixStack->at( k )->get( i, j );
		pmOutputVector->set( 0, k, value );
	}
}

// static
void GMatrixf::putVectorIntoMatrixStack( GMatrixf* pmInputVector,
										vector< GMatrixf* >* pvMatrixStack,
										int i, int j )
{
	for( uint k = 0; k < pvMatrixStack->size(); ++k )
	{
		float value = pmInputVector->get( 0, k );
		pvMatrixStack->at( k )->set( i, j, value );
	}
}

bool GMatrixf::solveConjugateGradient( GMatrixf* pmA, GMatrixf* pmB, GMatrixf* pmOutX )
{
	if( ( pmA == NULL ) ||
		( pmB == NULL ) ||
		( pmOutX == NULL ) )
	{
		return false;
	}

	if( ( pmB->getNumCols() != 1 ) ||
		( pmOutX->getNumCols() != 1 ) ||
		( pmA->getNumRows() != pmB->getNumRows() ) ||
		( pmA->getNumRows() != pmOutX->getNumRows() ) )
	{
		return false;
	}

	int i;
	float fTolerance = 0.001f; // TODO: input tolerance
	int iMaxIterations = std::min( pmB->getNumRows(), 20 ); // TODO: numIterations
	int iRows = pmB->getNumRows();

	// check if B is all zeroes, if so, then we're done, return 0
	float fbNorm = pmB->columnNorm( 0 );
	if( fbNorm < fTolerance )
	{
		for( i = 0; i < iRows; ++i )
		{
			pmOutX->set( i, 1, 0 );
		}
		return true;
	}

	// Otherwise, initialize
	float fbTolerance = fTolerance * fbNorm;
	


	GMatrixf mResidual( iRows, 1 );
	GMatrixf mx( iRows, 1 ); // TODO: initial guess
	GMatrixf mAx( iRows, 1 );

	GMatrixf::multiply( pmA, &mx, &mAx );
	GMatrixf::subtract( pmB, &mAx, &mResidual );

	float fResidualNorm = mResidual.columnNorm( 0 );
	if( fResidualNorm < fbTolerance )
	{
		for( i = 0; i < iRows; ++i )
		{
			pmOutX->set( i, 1, mx.get( i, 1 ) ); // initial guess was good
		}
		return true;
	}

	// otherwise, start the party
	// no preconditioner, so z[j] is just r[j]
	int j;
	float previousRho;
	float currentRho = 0.0f;
	float beta;
	float pDotq;
	float alpha;
	GMatrixf mP( iRows, 1 );
	GMatrixf mBetaP( iRows, 1 );
	GMatrixf mQ( iRows, 1 );
	GMatrixf mAlphaP( iRows, 1 );
	GMatrixf mError( iRows, 1 );
	GMatrixf mAlphaQ( iRows, 1 );

	for( j = 0; j < iMaxIterations; ++j )
	{
		previousRho = currentRho;
		currentRho = GMatrixf::columnVectorDot( &mResidual, 0, &mResidual, 0 ); // = r dot r

		// TODO: check currentRho = 0 or infinity
		if( j == 0 )
		{
			mP.set( &mResidual );
		}
		else
		{
			beta = currentRho / previousRho;
			// TODO: check beta = 0 or infinity
			GMatrixf::multiply( beta, &mP, &mBetaP );
			GMatrixf::add( &mResidual, &mBetaP, &mP );
		}
		GMatrixf::multiply( pmA, &mP, &mQ ); // q = A * p
		pDotq = GMatrixf::columnVectorDot( &mP, 0, &mQ, 0 ); // pDotq = p dot q

		if( ( pDotq <= 0 ) || isinf( pDotq ) )
		{
			pmOutX->set( &mx );
			return true;
		}
		alpha = currentRho / pDotq;

		// TODO: check alpha infinity => error, 0 => stagnation
		GMatrixf::multiply( alpha, &mP, &mAlphaP );
		GMatrixf::add( &mx, &mAlphaP, &mx );

		// check convergence
		GMatrixf::multiply( pmA, &mx, &mAx );
		GMatrixf::subtract( pmB, &mAx, &mError );
		fResidualNorm = mError.columnNorm( 0 );

		// converged
		if( fResidualNorm < fbTolerance )
		{
			pmOutX->set( &mx );
			return true;
		}
		// otherwise
		else
		{
			GMatrixf::multiply( alpha, &mQ, &mAlphaQ );
			GMatrixf::subtract( &mResidual, &mAlphaQ, &mResidual );
		}
	}

	pmOutX->set( &mx );
	return true;
}

float GMatrixf::rowNorm( int iRow )
{
	return sqrt( rowNormSquared( iRow ) );
}

float GMatrixf::columnNorm( int iColumn )
{
	return sqrt( columnNormSquared( iColumn ) );
}

float GMatrixf::rowNormSquared( int iRow )
{
	float v;
	float sum = 0;

	for( int i = 0; i < m_iCols; ++i )
	{
		v = get( iRow, i );
		sum += v * v;
	}

	return sum;
}

float GMatrixf::columnNormSquared( int iColumn )
{
	float v;
	float sum = 0;

	for( int i = 0; i < m_iRows; ++i )
	{
		v = get( i, iColumn );
		sum += v * v;
	}

	return sum;
}

void GMatrixf::getMinMax( float* pfMin, float* pfMax )
{
	float fMin = m_afElements[0];
	float fMax = m_afElements[0];

	for( int i = 1; i < ( m_iRows * m_iCols ); ++i )
	{
		float val = m_afElements[i];
		if( val < fMin )
		{
			fMin = val;
		}
		if( val > fMax )
		{
			fMax = val;
		}
	}

	*pfMin = fMin;
	*pfMax = fMax;
}

// static
float GMatrixf::rowVectorDot( GMatrixf* pmA, int iRowA, GMatrixf* pmB, int iRowB )
{
	float sum = 0;

	for( int i = 0; i < pmA->getNumCols(); ++i )
	{
		sum += pmA->get( iRowA, i ) * pmB->get( iRowB, i );
	}

	return sum;
}

// static
float GMatrixf::columnVectorDot( GMatrixf* pmA, int iColA, GMatrixf* pmB, int iColB )
{
	float sum = 0;

	for( int i = 0; i < pmA->getNumRows(); ++i )
	{
		sum += pmA->get( i, iColA ) * pmB->get( i, iColB );
	}

	return sum;
}
#endif