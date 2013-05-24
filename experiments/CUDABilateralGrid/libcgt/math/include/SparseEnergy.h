#pragma once

#include <cholmod.h>
#include <SparseMatrix.h>

class FloatMatrix;

// interface
class SparseEnergy
{
public:

	virtual int numFunctions() = 0;
	virtual int numVariables() = 0;
	virtual int maxNumNonZeroes() = 0;

	virtual void evaluateInitialGuess( FloatMatrix& guess ) = 0;

	// Evaluate the residual of the energy and its Jacobian at argument beta, where:
	//
	// input:
	//   beta is a numVariables x 1 vector
	//   
	// output:
	//   residual is a numFunctions x 1 vector
	//   J is a numFunctions x numVariables sparse matrix
	//     J(i,j) = \frac{ \partial r_i }{ \partial \Beta_j } | \Beta

	// and return it in r (a numFunctions x 1 vector)
	virtual void evaluateResidualAndJacobian( const FloatMatrix& beta,
		FloatMatrix& residual, cholmod_triplet* J ) = 0;

	virtual void evaluateResidualAndJacobian( const FloatMatrix& beta,
		FloatMatrix& residual, CoordinateSparseMatrix< float >& J ) = 0;

	virtual void evaluateResidualAndJacobian( const FloatMatrix& beta,
		FloatMatrix& residual, CompressedSparseMatrix< float >& J ) = 0;
};
