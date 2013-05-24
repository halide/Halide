#ifndef GAUSS_NEWTON_H
#define GAUSS_NEWTON_H

#include <memory>

#include "Energy.h"
#include "FloatMatrix.h"

class GaussNewton
{
public:

	// Initialize Gauss-Newton solver
	// pEnergy->numFunctions() >= pEnergy->numParameters()

	// Parameters:
	//   epsilon: minimize() will run until the residual has squared norm < epsilon
	//   or...
	//
	//   maxNumIterations n: minimize() will run for at most n iterations.
	//   Set to a negative number to ignore
	//
	GaussNewton( std::shared_ptr< Energy > pEnergy,
		int maxNumIterations = 100,
		float epsilon = 1e-6 );

	int maxNumIterations() const;
	void setMaxNumIterations( int maxNumIterations );

	float epsilon() const;
	void setEpsilon( float epsilon );

	FloatMatrix minimize( float* pEnergyFound = nullptr, int* pNumIterations = nullptr );

private:

	std::shared_ptr< Energy > m_pEnergy;
	int m_maxNumIterations;
	float m_epsilon;

	FloatMatrix m_J;
	FloatMatrix m_prevBeta;
	FloatMatrix m_currBeta;
	FloatMatrix m_delta;
	FloatMatrix m_r;

};

#endif // GAUSS_NEWTON_H
