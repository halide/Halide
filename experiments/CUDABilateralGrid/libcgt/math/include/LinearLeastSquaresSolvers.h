#ifndef LINEAR_LEAST_SQUARES_SOLVERS
#define LINEAR_LEAST_SQUARES_SOLVERS

#include "FloatMatrix.h"

class LinearLeastSquaresSolvers
{
public:

	// Calls MKL's sgels to use QR / LQ to solve a over/under-determined
	// linear system with full rank
	// A: m x n, is full rank
	// b: m x k, k right hand sides
	// x: n x k, k solution vectors of length m each
	// 
	// If m >= n, then solve for the least squares solution min |b-aX|^2 (overdetermined)
	// if m < n, then solve for the minimum norm solution (underdetermined)
	static FloatMatrix QRFullRank( const FloatMatrix& a, const FloatMatrix& b, bool* succeeded = nullptr );
	static bool QRFullRank( const FloatMatrix& a, const FloatMatrix& b, FloatMatrix& x );

	// both a and b are overwritten
	// b[ 0:a.numCols(), 0 ] has the solution
	static bool QRFullRankInPlace( FloatMatrix& a, FloatMatrix& b );
	
	// Solve for Ax ~ b
	// for a *potentially* rank-deficient matrix A
	// (simultaneously find x that minimizes ||b-Ax||^2 and ||x||^2)
	// rCond specifies a reciprocal condition number threshold such that
	// singular values s[i] <= rCond * s[0] are considered zeroed out
	// rCond = -1 --> defaults to machine precision

	// returns x
	static FloatMatrix SVDRankDeficient( const FloatMatrix& a, const FloatMatrix& b, float rCond = -1 );
	
	static void SVDRankDeficient( const FloatMatrix& a, const FloatMatrix& b,
		FloatMatrix& x, float rCond = -1 );

	// both a and b are overwritten
	// b[ 0:a.numCols(), 0 ] has the solution
	static void SVDRankDeficientInPlace( FloatMatrix& a, FloatMatrix& b, float rCond );

};

#endif // LINEAR_LEAST_SQUARES_SOLVERS
