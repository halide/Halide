#include "SparseGaussNewton.h"

#include <limits>
#include <QtGlobal>

#include <SuiteSparseQR.hpp>

void copyFloatMatrixToCholmodDense( const FloatMatrix& src, cholmod_dense* dst )
{
	double* dstArray = reinterpret_cast< double* >( dst->x );
	for( int k = 0; k < src.numElements(); ++k )
	{
		dstArray[k] = src[k];
	}
}

void copyCholmodDenseToFloatMatrix( cholmod_dense* src, FloatMatrix& dst )
{
	double* srcArray = reinterpret_cast< double* >( src->x );
	for( int k = 0; k < dst.numElements(); ++k )
	{
		dst[k] = srcArray[k];
	}
}

void copyDoubleArrayToFloatMatrix( double* srcArray, FloatMatrix& dst )
{
	for( int k = 0; k < dst.numElements(); ++k )
	{
		dst[k] = srcArray[k];
	}
}

void saveVector( const FloatMatrix& x, QString filename )
{
	FILE* fp = fopen( qPrintable( filename ), "w" );
	for( int i = 0; i < x.numElements(); ++i )
	{
		fprintf( fp, "%f\n", x[i] );
	}
	fclose( fp );
}

#define TIMING 1
#define FACTORIZE 0
#define SPLIT_FACTORIZATION 0

#if TIMING
#include <time/StopWatch.h>
#endif


SparseGaussNewton::SparseGaussNewton( std::shared_ptr< SparseEnergy > pEnergy, cholmod_common* pcc,
	int maxNumIterations, float epsilon ) :

	m_maxNumIterations( maxNumIterations ),

	m_pcc( pcc ),
	m_J( nullptr ),
	m_pFactorization( nullptr ),

	m_r2( nullptr ),


	m_L( nullptr ),
	m_jtr2( nullptr ),

	m_cscJtJ( SYMMETRIC )

{
	setEpsilon( epsilon );
	setEnergy( pEnergy );	
}

SparseGaussNewton::~SparseGaussNewton()
{
	if( m_r2 != nullptr )
	{
		cholmod_l_free_dense( &m_r2, m_pcc );
	}
	if( m_pFactorization != nullptr )
	{
		SuiteSparseQR_free< double >( &m_pFactorization, m_pcc );
	}
	if( m_J != nullptr )
	{
		cholmod_l_free_triplet( &m_J, m_pcc );
	}	
	m_pcc = nullptr;
}

void SparseGaussNewton::setEnergy( std::shared_ptr< SparseEnergy > pEnergy )
{
	printf( "resetting energy!\n" );

	m_pEnergy = pEnergy;

	int m = pEnergy->numFunctions();
	int n = pEnergy->numVariables();
	Q_ASSERT_X( m >= n, "Gauss Newton", "Number of functions (m) must be greater than the number of parameters (n)." );

	m_prevBeta.resize( n, 1 );
	m_currBeta.resize( n, 1 );
	m_delta.resize( n, 1 );
	m_r.resize( m, 1 );

	int nzMax = pEnergy->maxNumNonZeroes();

	// if m_r2 already exists
	if( m_r2 != nullptr )
	{
		// and the sizes don't match
		if( m_r2->nrow != m )
		{
			// then free it and re-allocate
			cholmod_l_free_dense( &m_r2, m_pcc );
			// TODO: use realloc instead?
			m_r2 = cholmod_l_allocate_dense( m, 1, m, CHOLMOD_REAL, m_pcc );
		}
	}
	else
	{
		m_r2 = cholmod_l_allocate_dense( m, 1, m, CHOLMOD_REAL, m_pcc );
	}

	if( m_J != nullptr )
	{
		if( m_J->nrow != m ||
			m_J->ncol != n ||
			m_J->nzmax != nzMax )
		{
			cholmod_l_free_triplet( &m_J, m_pcc );
			// TODO: use realloc instead?
			m_J = cholmod_l_allocate_triplet( m, n, nzMax, 0, CHOLMOD_REAL, m_pcc );
		}
	}
	else
	{
		m_J = cholmod_l_allocate_triplet( m, n, nzMax, 0, CHOLMOD_REAL, m_pcc );	
	}

	if( m_pFactorization != nullptr )
	{
		SuiteSparseQR_free< double >( &m_pFactorization, m_pcc );
	}

	// if m_jtr2 already exists
	if( m_jtr2 != nullptr )
	{
		// and the sizes don't match
		if( m_jtr2->nrow != n )
		{
			// then free it and re-allocate
			cholmod_l_free_dense( &m_jtr2, m_pcc );
			m_jtr2 = cholmod_l_allocate_dense( n, 1, n, CHOLMOD_REAL, m_pcc );
		}
	}
	else
	{
		m_jtr2 = cholmod_l_allocate_dense( n, 1, n, CHOLMOD_REAL, m_pcc );
	}
	if( m_L != nullptr )
	{
		cholmod_l_free_factor( &m_L, m_pcc );
	}

	// try PARDISO
	m_alreadySetup = false;
	m_coordJ.clear();
	m_coordJtJ.clear();
	// HACK
	m_coordJ.reserve( m * n );
	m_coordJtJ.reserve( n * n );
	m_jtr.resize( n, 1 );
}

uint SparseGaussNewton::maxNumIterations() const
{
	return m_maxNumIterations;
}

void SparseGaussNewton::setMaxNumIterations( uint maxNumIterations )
{
	m_maxNumIterations = maxNumIterations;
}

float SparseGaussNewton::epsilon() const
{
	return m_epsilon;
}

void SparseGaussNewton::setEpsilon( float epsilon )
{
	m_epsilon = epsilon;
	m_sqrtEpsilon = sqrtf( epsilon );
}

const FloatMatrix& SparseGaussNewton::minimize( float* pEnergyFound, int* pNumIterations )
{
#if TIMING
	float tR = 0;
	float tCopy = 0;
	float tConvert = 0;
#if FACTORIZE
	float tFactorize = 0;
	float tQMult = 0;
	float tSolve = 0;
#else
	float tQR = 0;
#endif
	StopWatch sw;
#endif
	
	m_pEnergy->evaluateInitialGuess( m_currBeta );

#if TIMING
	sw.reset();
#endif
	m_J->nnz = 0;
	m_pEnergy->evaluateResidualAndJacobian( m_currBeta, m_r, m_J );
#if TIMING
	tR += sw.millisecondsElapsed();
	sw.reset();
#endif
	copyFloatMatrixToCholmodDense( m_r, m_r2 );
#if TIMING
	tCopy += sw.millisecondsElapsed();
#endif

	float prevEnergy;
	float currEnergy = FloatMatrix::dot( m_r, m_r );
	float deltaEnergy;

	bool deltaEnergyConverged;
	bool deltaBetaConverged;
	bool converged = false;

	//printf( "initial energy = %f\n", currEnergy );	

	// check for convergence
	int nIterations = 0;
	while( ( nIterations < m_maxNumIterations ) &&
		!converged )
	{
		// not converged
		prevEnergy = currEnergy;
		//m_prevBeta = m_currBeta;

#if 0
		if( nIterations == 0 )
		{
			//printf( "prev beta = %s\n", qPrintable( m_prevBeta.toString() ) );
			//printf( "curr beta = %s\n", qPrintable( m_currBeta.toString() ) );
			//printf( "curr residual =\n%s\n", qPrintable( m_r.toString() ) );
			saveVector( m_r, "c:/tmp/r0.txt" );
		}
#endif

#if TIMING
		sw.reset();
#endif
		cholmod_sparse* jSparse = cholmod_l_triplet_to_sparse( m_J, m_J->nnz, m_pcc );		
#if TIMING
		tConvert += sw.millisecondsElapsed();
		sw.reset();
#endif
		
#if FACTORIZE

#if SPLIT_FACTORIZATION // symbolic then numeric
		if( m_pFactorization == nullptr )
		{
			m_pFactorization = SuiteSparseQR_symbolic< double >( SPQR_ORDERING_DEFAULT, false, jSparse, m_pcc );
		}
		SuiteSparseQR_numeric< double >( SPQR_DEFAULT_TOL, jSparse, m_pFactorization, m_pcc );
#else
		// numeric directly
		if( m_pFactorization == nullptr )
		{
			//printf( "factorization is null, factorizing...\n" );
			//m_pFactorization = SuiteSparseQR_factorize< double >( SPQR_ORDERING_DEFAULT, SPQR_DEFAULT_TOL, jSparse, m_pcc );
			//m_pFactorization = SuiteSparseQR_factorize< double >( SPQR_ORDERING_DEFAULT, 1e-3, jSparse, m_pcc );
			//m_pFactorization = SuiteSparseQR_factorize< double >( SPQR_ORDERING_CHOLMOD, 1e-3, jSparse, m_pcc );
			m_pFactorization = SuiteSparseQR_factorize< double >( SPQR_ORDERING_CHOLMOD, SPQR_DEFAULT_TOL, jSparse, m_pcc );
			//m_pFactorization = SuiteSparseQR_factorize< double >( SPQR_ORDERING_METIS, 1e-3, jSparse, m_pcc );
			//m_pFactorization = SuiteSparseQR_factorize< double >( SPQR_ORDERING_BEST, 1e-3, jSparse, m_pcc );
			//m_pFactorization = SuiteSparseQR_factorize< double >( SPQR_ORDERING_COLAMD, 1e-3, jSparse, m_pcc );
			//m_pFactorization = SuiteSparseQR_factorize< double >( SPQR_ORDERING_AMD, 1e-3, jSparse, m_pcc );			
		}
		else
		{
			//printf( "factorization exists, reusing...\n" );
			SuiteSparseQR_numeric< double >( SPQR_DEFAULT_TOL, jSparse, m_pFactorization, m_pcc );
			//SuiteSparseQR_numeric< double >( 1e-3, jSparse, m_pFactorization, m_pcc );
		}
#endif

#if TIMING
		tFactorize += sw.millisecondsElapsed();
		sw.reset();
#endif

		auto y = SuiteSparseQR_qmult< double >( SPQR_QTX, m_pFactorization, m_r2, m_pcc );

#if TIMING
		tQMult += sw.millisecondsElapsed();
		sw.reset();
#endif
		auto delta = SuiteSparseQR_solve< double >( SPQR_RETX_EQUALS_B, m_pFactorization, y, m_pcc );
#if TIMING
		tSolve += sw.millisecondsElapsed();
#endif

#else
		auto delta = SuiteSparseQR< double >( jSparse, m_r2, m_pcc );
#if TIMING
		tQR += sw.millisecondsElapsed();
#endif

#endif

#if TIMING
		sw.reset();
#endif
		copyCholmodDenseToFloatMatrix( delta, m_delta );
#if TIMING
		tCopy += sw.millisecondsElapsed();
#endif

		m_J->nnz = 0; // reset sparse jacobian
		cholmod_l_free_dense( &delta, m_pcc );
#if FACTORIZE
		cholmod_l_free_dense( &y, m_pcc );
#endif
		cholmod_l_free_sparse( &jSparse, m_pcc );
		
		//m_currBeta = m_prevBeta - m_delta;
		m_currBeta -= m_delta;

#if 0
		if( nIterations == 0 )
		{
			saveVector( m_delta, "c:/tmp/delta_0.txt" );
			saveVector( m_prevBeta, "c:/tmp/m_prevBeta.txt" );
			saveVector( m_currBeta, "c:/tmp/m_currBeta.txt" );
			//printf( "initial delta =\n%s\n", qPrintable( m_delta.toString() ) );
			//printf( "beta[1] =\n%s\n", qPrintable( m_currBeta.toString() ) );
		}
#endif

		// update energy
#if TIMING
		sw.reset();
#endif
		m_pEnergy->evaluateResidualAndJacobian( m_currBeta, m_r, m_J );
#if TIMING
		tR += sw.millisecondsElapsed();
		sw.reset();
#endif
		copyFloatMatrixToCholmodDense( m_r, m_r2 );
#if TIMING
		tCopy += sw.millisecondsElapsed();
#endif

		currEnergy = FloatMatrix::dot( m_r, m_r );
		deltaEnergy = fabs( currEnergy - prevEnergy );

		deltaEnergyConverged = ( deltaEnergy < m_epsilon * ( 1 + currEnergy ) );

#if 0
		//float deltaBetaMax = m_delta.maximum();
		//deltaBetaConverged = ( deltaBetaMax < m_sqrtEpsilon * ( 1 + deltaBetaMax ) );		
		converged = deltaEnergyConverged && deltaBetaConverged;
#else
		converged = deltaEnergyConverged;

		//printf( "k = %d, E[k] = %f, |deltaE| = %f, eps * ( 1 + E[k] ) = %f, converged = %d\n",
		//	nIterations, currEnergy, deltaEnergy, m_epsilon * ( 1 + currEnergy ), (int)deltaEnergyConverged );

#endif
		++nIterations;
	}

#if TIMING
#if FACTORIZE
	printf( "timing breakdown:\ntR = %f, tCopy = %f, tConvert = %f, tFactorize = %f, tQMult = %f, tSolve = %f\n",
		tR, tCopy, tConvert, tFactorize, tQMult, tSolve );
#else
	printf( "timing breakdown:\ntR = %f, tCopy = %f, tConvert = %f, tQR = %f\n",
		tR, tCopy, tConvert, tQR );
#endif
#endif

	if( pEnergyFound != nullptr )
	{
		*pEnergyFound = currEnergy;
	}

	if( pNumIterations != nullptr )
	{
		*pNumIterations = nIterations;
	}
	return m_currBeta;
}

void save_triplet( cholmod_triplet* A, const char* filename )
{
	FILE* fp = fopen( filename, "w" );

	fprintf( fp, "%d\t%d\t-1\n", A->nrow, A->ncol );

	__int64* i = ( __int64* )( A->i );
	__int64* j = ( __int64* )( A->j );
	double* x = ( double* )( A->x );
	for( int k = 0; k < A->nnz; ++k )
	{
		int ii = ( int )(i[k]);
		int jj = ( int )(j[k]);
		fprintf( fp, "%d\t%d\t%lf\n", ii + 1, jj + 1, x[k] );
	}

	fclose( fp );
}

void save_sparse( cholmod_sparse* A, const char* filename, cholmod_common* cc )
{
	auto A_triplet = cholmod_l_sparse_to_triplet( A, cc );
	save_triplet( A_triplet, filename );
	cholmod_l_free_triplet( &A_triplet, cc );
}

void save_dense( cholmod_dense* A, const char* filename )
{
	FILE* fp = fopen( filename, "w" );
	double* x = ( double* )( A->x );
	for( int i = 0; i < A->nrow; ++i )
	{
		fprintf( fp, "%lf\n", x[i] );
	}
	fclose( fp );
}

void print_triplet( cholmod_triplet* A )
{
	__int64* i = ( __int64* )( A->i );
	__int64* j = ( __int64* )( A->j );
	double* x = ( double* )( A->x );
	for( int k = 0; k < A->nnz; ++k )
	{
		int ii = ( int )(i[k]);
		int jj = ( int )(j[k]);
		printf( "(%d,%d): %lf\n", ii, jj, x[k] );
	}
}

void print_sparse( cholmod_sparse* A, cholmod_common* cc )
{
	auto A_triplet = cholmod_l_sparse_to_triplet( A, cc );
	print_triplet( A_triplet );
	cholmod_l_free_triplet( &A_triplet, cc );
}

const FloatMatrix& SparseGaussNewton::minimize2( float* pEnergyFound, int* pNumIterations )
{
#if TIMING
	StopWatch sw;
	float tSSMult = 0;
	float tFactorize = 0;
	float tSolve = 0;
#endif

	double alpha[2] = { 1, 1 };
	double beta[2] = { 0, 0 };

	m_pEnergy->evaluateInitialGuess( m_currBeta );

	m_J->nnz = 0;
	m_pEnergy->evaluateResidualAndJacobian( m_currBeta, m_r, m_J );
	copyFloatMatrixToCholmodDense( m_r, m_r2 );
#if 0
	printf( "J is %d x %d\n", m_J->nrow, m_J->ncol );
	QString jFilename = QString( "c:/tmp/j_%1.txt" ).arg( 0, 5, 10, QChar( '0' ) );
	QString rFilename = QString( "c:/tmp/r_%1.txt" ).arg( 0, 5, 10, QChar( '0' ) );
	printf( "saving: %s\n", qPrintable( jFilename ) );
	save_triplet( m_J, qPrintable( jFilename ) );
	printf( "saving: %s\n", qPrintable( rFilename ) );
	save_dense( m_r2, qPrintable( rFilename ) );
#endif

	float prevEnergy;
	float currEnergy = FloatMatrix::dot( m_r, m_r );
	float deltaEnergy;

	bool deltaEnergyConverged;
	bool deltaBetaConverged;
	bool converged = false;

	// check for convergence
	int nIterations = 0;
	while( ( nIterations < m_maxNumIterations ) &&
		!converged )
	{
		// not converged
		prevEnergy = currEnergy;

		cholmod_sparse* jSparse = cholmod_l_triplet_to_sparse( m_J, m_J->nnz, m_pcc );
		
		// compute J'
		cholmod_sparse* jtSparse = cholmod_l_transpose( jSparse, 1, m_pcc );

		// compute J'J
#if TIMING
		sw.reset();
#endif
		cholmod_sparse* jtjSparse = cholmod_l_ssmult( jtSparse, jSparse, -1, true, true, m_pcc );
#if TIMING
		tSSMult += sw.millisecondsElapsed();
#endif

		// compute J' * r
		cholmod_l_sdmult( jtSparse, 0, alpha, beta, m_r2, m_jtr2, m_pcc );		

		// analyze
		if( m_L == nullptr )
		{
			m_L = cholmod_l_analyze( jtjSparse, m_pcc );
		}
		// factorize
#if TIMING
		sw.reset();
#endif
		cholmod_l_factorize( jtjSparse, m_L, m_pcc );
#if TIMING
		tFactorize += sw.millisecondsElapsed();
#endif
		// solve using factorization
#if TIMING
		sw.reset();
#endif
		cholmod_dense* delta = cholmod_l_solve( CHOLMOD_A, m_L, m_jtr2, m_pcc );
#if TIMING
		tSolve += sw.millisecondsElapsed();
#endif

		copyCholmodDenseToFloatMatrix( delta, m_delta );

		m_J->nnz = 0; // reset sparse jacobian
		cholmod_l_free_dense( &delta, m_pcc );
		cholmod_l_free_sparse( &jtjSparse, m_pcc );
		cholmod_l_free_sparse( &jtSparse, m_pcc );
		cholmod_l_free_sparse( &jSparse, m_pcc );

		m_currBeta -= m_delta;
		// update energy
		m_pEnergy->evaluateResidualAndJacobian( m_currBeta, m_r, m_J );
		copyFloatMatrixToCholmodDense( m_r, m_r2 );
#if 0
		QString jFilename = QString( "c:/tmp/j_%1.txt" ).arg( nIterations, 5, 10, QChar( '0' ) );
		QString rFilename = QString( "c:/tmp/r_%1.txt" ).arg( nIterations, 5, 10, QChar( '0' ) );
		printf( "saving: %s\n", qPrintable( jFilename ) );
		save_triplet( m_J, qPrintable( jFilename ) );
		printf( "saving: %s\n", qPrintable( rFilename ) );
		save_dense( m_r2, qPrintable( rFilename ) );
#endif
		currEnergy = FloatMatrix::dot( m_r, m_r );
		deltaEnergy = fabs( currEnergy - prevEnergy );

		deltaEnergyConverged = ( deltaEnergy < m_epsilon * ( 1 + currEnergy ) );

#if 0
		//float deltaBetaMax = m_delta.maximum();
		//deltaBetaConverged = ( deltaBetaMax < m_sqrtEpsilon * ( 1 + deltaBetaMax ) );		
		converged = deltaEnergyConverged && deltaBetaConverged;
#else
		converged = deltaEnergyConverged;

		//printf( "k = %d, E[k] = %f, |deltaE| = %f, eps * ( 1 + E[k] ) = %f, converged = %d\n",
		//	nIterations, currEnergy, deltaEnergy, m_epsilon * ( 1 + currEnergy ), (int)deltaEnergyConverged );

#endif
		++nIterations;
	}
#if 0
	exit(0);
#endif

#if TIMING
	printf( "sparse * sparse took %f ms\n", tSSMult );
	printf( "factorize took %f ms\n", tFactorize );
	printf( "solve took %f ms\n", tSolve );
#endif

	if( pEnergyFound != nullptr )
	{
		*pEnergyFound = currEnergy;
	}

	if( pNumIterations != nullptr )
	{
		*pNumIterations = nIterations;
	}
	return m_currBeta;
}

#if 1
const FloatMatrix& SparseGaussNewton::minimize3( float* pEnergyFound, int* pNumIterations )
{
#if TIMING
	StopWatch sw;
	float tCompress0 = 0;
	float tSSMult0 = 0;
	float tCompress1 = 0;
	float tSVMult = 0;
	float tSSMult = 0;
	float tFactorize = 0;
	float tSolve = 0;
#endif

	m_pEnergy->evaluateInitialGuess( m_currBeta );

	// TODO: if not already set up, then call it with coordinate
	// otherwise, call it with csc
	// compute transpose...
	m_pEnergy->evaluateResidualAndJacobian( m_currBeta, m_r, m_coordJ );
#if TIMING
	sw.reset();
#endif
	m_coordJ.compress( m_cscJ );
	m_coordJ.compressTranspose( m_cscJt );
#if TIMING
	tCompress0 += sw.millisecondsElapsed();
#endif

#if TIMING
	sw.reset();
#endif
	CompressedSparseMatrix< float >::multiply( m_cscJt, m_cscJ, m_cscJtJ );
#if TIMING
	tSSMult0 += sw.millisecondsElapsed();
#endif

	printf( "compress0 took %f ms\n", tCompress0 );
	printf( "ssmult0 took %f ms\n", tSSMult0 );

	exit( 0 );

#if TIMING
	sw.reset();
#endif
	m_coordJtJ.compress( m_cscJtJ );
#if TIMING
	tCompress1 += sw.millisecondsElapsed();
#endif

	float prevEnergy;
	float currEnergy = FloatMatrix::dot( m_r, m_r );
	float deltaEnergy;

	bool deltaEnergyConverged;
	bool deltaBetaConverged;
	bool converged = false;

	// check for convergence
	int nIterations = 0;
	while( ( nIterations < m_maxNumIterations ) &&
		!converged )
	{
		// not converged
		prevEnergy = currEnergy;

		// compute J' * r
#if TIMING
		sw.reset();
#endif
		m_cscJ.multiplyTransposeVector( m_r, m_jtr );
#if TIMING
		tSVMult += sw.millisecondsElapsed();
#endif

		// compute J'J
#if TIMING
		sw.reset();
#endif
		m_cscJ.multiplyTranspose( m_cscJtJ );
#if TIMING
		tSSMult += sw.millisecondsElapsed();
#endif		

		// analyze
		if( !m_alreadySetup )
		{
			m_pardiso.analyzePattern( m_cscJtJ );
			m_alreadySetup = true;
		}
		// factorize
#if TIMING
		sw.reset();
#endif
		m_pardiso.factorize( m_cscJtJ );
#if TIMING
		tFactorize += sw.millisecondsElapsed();
#endif
		// solve using factorization
#if TIMING
		sw.reset();
#endif
		m_pardiso.solve( m_jtr, m_delta );
#if TIMING
		tSolve += sw.millisecondsElapsed();
#endif

		m_currBeta -= m_delta;
		// update energy
		m_pEnergy->evaluateResidualAndJacobian( m_currBeta, m_r, m_cscJ );

		currEnergy = FloatMatrix::dot( m_r, m_r );
		deltaEnergy = fabs( currEnergy - prevEnergy );

		deltaEnergyConverged = ( deltaEnergy < m_epsilon * ( 1 + currEnergy ) );

#if 0
		//float deltaBetaMax = m_delta.maximum();
		//deltaBetaConverged = ( deltaBetaMax < m_sqrtEpsilon * ( 1 + deltaBetaMax ) );		
		converged = deltaEnergyConverged && deltaBetaConverged;
#else
		converged = deltaEnergyConverged;

		//printf( "k = %d, E[k] = %f, |deltaE| = %f, eps * ( 1 + E[k] ) = %f, converged = %d\n",
		//	nIterations, currEnergy, deltaEnergy, m_epsilon * ( 1 + currEnergy ), (int)deltaEnergyConverged );

#endif
		++nIterations;
	}

#if TIMING

	printf( "J'J is %d x %d\n", m_coordJtJ.numRows(), m_coordJtJ.numCols() );

	printf( "compress0 took %f ms\n", tCompress0 );
	printf( "ssmult0 took %f ms\n", tSSMult0 );
	printf( "compress1 took %f ms\n", tCompress1 );
	printf( "svMult took %f ms\n", tSVMult );

	printf( "sparse * sparse took %f ms\n", tSSMult );
	printf( "factorize took %f ms\n", tFactorize );
	printf( "solve took %f ms\n", tSolve );
#endif

	if( pEnergyFound != nullptr )
	{
		*pEnergyFound = currEnergy;
	}

	if( pNumIterations != nullptr )
	{
		*pNumIterations = nIterations;
	}

	return m_currBeta;
}
#endif