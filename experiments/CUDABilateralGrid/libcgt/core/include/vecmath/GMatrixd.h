#if 0

#ifndef GMATRIXD_H
#define GMATRIXD_H

#include "vecmath/Vector3f.h"

#include <QVector>

#include <gsl/gsl_eigen.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_permutation.h>

class GMatrixd
{
public:

	GMatrixd(); // makes a 0x0 matrix
	GMatrixd( int nRows, int nCols, double fillValue = 0 );
	GMatrixd( const GMatrixd& m );
	virtual ~GMatrixd();
	GMatrixd& operator = ( const GMatrixd& m );

	bool isNull() const; // a matrix is null if either nRows or nCols is 0

	void fill( double d );

	int numRows() const;
	int numCols() const;
	int numElements() const;
	void indexToSubscript( int idx, int* pi, int* pj ) const;
	int subscriptToIndex( int i, int j ) const;

	void resize( int nRows, int nCols );

	void reshape( int nRows, int nCols );

	// access at ( i, j )
	double& operator () ( int i, int j );
	const double& operator () ( int i, int j ) const;

	// access element k with column-major ordering
	double& operator [] ( int k );
	const double& operator [] ( int k ) const;

	double* data();

	void transpose( GMatrixd& t );

	void eigenvalueDecomposition( QVector< QVector< double > >* eigen_vector,
		QVector< double >* eigen_value );

	void foo();

	static void times( const GMatrixd& a, const GMatrixd& b, GMatrixd& c );

	static void homography( QVector< Vector3f > from,
		QVector< Vector3f > to, GMatrixd& output );

	void print();

private:

	int m_nRows;
	int m_nCols;
	QVector< double > m_data;
};

#endif // GMATRIXD_H

#endif