#include "GaussNewton.h"

#include <limits>
#include <QtGlobal>

#include "LinearLeastSquaresSolvers.h"

GaussNewton::GaussNewton( std::shared_ptr< Energy > pEnergy,
	int maxNumIterations,
	float epsilon ) :

	m_pEnergy( pEnergy ),
	m_maxNumIterations( maxNumIterations ),
	m_epsilon( epsilon )

{
	int m = pEnergy->numFunctions();
	int n = pEnergy->numVariables();

	m_J.resize( m, n );
	m_prevBeta.resize( n, 1 );
	m_currBeta.resize( n, 1 );
	m_delta.resize( n, 1 );
	m_r.resize( m, 1 );

	Q_ASSERT_X( m >= n, "Gauss Newton", "Number of functions (m) must be greater than the number of parameters (n)." );
}

int GaussNewton::maxNumIterations() const
{
	return m_maxNumIterations;
}

void GaussNewton::setMaxNumIterations( int maxNumIterations )
{
	m_maxNumIterations = maxNumIterations;
}

float GaussNewton::epsilon() const
{
	return m_epsilon;
}

void GaussNewton::setEpsilon( float epsilon )
{
	m_epsilon = epsilon;
}

FloatMatrix GaussNewton::minimize( float* pEnergyFound, int* pNumIterations )
{
	m_pEnergy->evaluateInitialGuess( m_currBeta );
	m_pEnergy->evaluateResidual( m_currBeta, m_r );

	float prevEnergy = FLT_MAX;
	float currEnergy = FloatMatrix::dot( m_r, m_r );
	float deltaEnergy = fabs( currEnergy - prevEnergy );
	
	// check for convergence
	int nIterations = 0;
	while( ( deltaEnergy > m_epsilon * ( 1 + currEnergy ) ) &&
		( ( m_maxNumIterations > 0 ) && ( nIterations < m_maxNumIterations ) ) )
	{
		// not converged
		prevEnergy = currEnergy;
		m_prevBeta = m_currBeta;

		// take a step:			
		m_pEnergy->evaluateJacobian( m_currBeta, m_J );
		//J.print( "J = " );

		// solve for -delta
		LinearLeastSquaresSolvers::QRFullRank( m_J, m_r, m_delta );
		// TODO: OPTIMIZE: do it in place
		m_currBeta = m_prevBeta - m_delta;

		// update energy
		m_pEnergy->evaluateResidual( m_currBeta, m_r );
		currEnergy = FloatMatrix::dot( m_r, m_r );
		deltaEnergy = fabs( currEnergy - prevEnergy );
		++nIterations;
	}

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
