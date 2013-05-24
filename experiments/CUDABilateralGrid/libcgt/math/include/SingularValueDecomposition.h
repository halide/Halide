#ifndef SINGULAR_VALUE_DECOMPOSITION_H
#define SINGULAR_VALUE_DECOMPOSITION_H

#include <memory>
#include "FloatMatrix.h"

class SingularValueDecomposition
{
public:
				
	// Computes the SVD of A:
	// A = U S V^T
	// For A: m x n
	// U needs to be m x m
	// S is min( m, n ) x 1
	// VT is n x n		
	static bool SVD( FloatMatrix* a, FloatMatrix* u, FloatMatrix* s, FloatMatrix* vt );
				
	static std::shared_ptr< SingularValueDecomposition > SVD( FloatMatrix* a );

	std::shared_ptr< FloatMatrix > u();
	std::shared_ptr< FloatMatrix > s();
	std::shared_ptr< FloatMatrix > vt();

private:

	SingularValueDecomposition( std::shared_ptr< FloatMatrix > u, std::shared_ptr< FloatMatrix > s, std::shared_ptr< FloatMatrix > vt );

	std::shared_ptr< FloatMatrix > m_u;
	std::shared_ptr< FloatMatrix > m_s;
	std::shared_ptr< FloatMatrix > m_vt;
};

#endif // SINGULAR_VALUE_DECOMPOSITION_H
