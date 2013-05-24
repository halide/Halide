#pragma once

#include <memory>
#include <common/BasicTypes.h>

#include "SparseEnergy.h"
#include "FloatMatrix.h"

#include <cholmod.h>
#include <SuiteSparseQR.hpp>

#include "SparseMatrix.h"
#include "PARDISOSolver.h"

class SparseGaussNewton
{
public:

	// Initialize Sparse Gauss-Newton solver
	// pEnergy->numFunctions() >= pEnergy->numParameters()
	//
	// Parameters:
	//   epsilon: minimize() will run until the residual has squared norm < epsilon
	//   or...
	//
	//   maxNumIterations n: minimize() will run for at most n iterations.
	//   Set to a negative number to ignore
	//
	SparseGaussNewton( std::shared_ptr< SparseEnergy > pEnergy, cholmod_common* pcc,
		int maxNumIterations = 100,
		float epsilon = 1e-6 );
	virtual ~SparseGaussNewton();

	void setEnergy( std::shared_ptr< SparseEnergy > pEnergy );

	uint maxNumIterations() const;
	void setMaxNumIterations( uint maxNumIterations );

	float epsilon() const;
	void setEpsilon( float epsilon );

	const FloatMatrix& minimize( float* pEnergyFound = nullptr, int* pNumIterations = nullptr );

	const FloatMatrix& minimize2( float* pEnergyFound = nullptr, int* pNumIterations = nullptr );

	const FloatMatrix& minimize3( float* pEnergyFound = nullptr, int* pNumIterations = nullptr );

private:

	std::shared_ptr< SparseEnergy > m_pEnergy;
	uint m_maxNumIterations;
	float m_epsilon;
	float m_sqrtEpsilon;

	cholmod_common* m_pcc;
	cholmod_triplet* m_J;	
	SuiteSparseQR_factorization< double >* m_pFactorization;

	FloatMatrix m_prevBeta;
	FloatMatrix m_currBeta;
	FloatMatrix m_delta;
	FloatMatrix m_r;
	cholmod_dense* m_r2;

	// for Cholesky:
	// solve J'J \Beta = J'r
	cholmod_factor* m_L;
	cholmod_dense* m_jtr2;

	// for PARDISO + Eigen
	CoordinateSparseMatrix< float > m_coordJ;
	CompressedSparseMatrix< float > m_cscJ;
	CompressedSparseMatrix< float > m_cscJt;

	CoordinateSparseMatrix< float > m_coordJtJ;
	CompressedSparseMatrix< float > m_cscJtJ;
	FloatMatrix m_jtr;

	// Eigen::VectorXf m_eJtR;

	bool m_alreadySetup;
	PARDISOSolver< float, true > m_pardiso;
};
