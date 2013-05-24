#pragma once

#include <QString>
#include <vector>

#include "vecmath/Vector2i.h"
#include "vecmath/Vector3f.h"

class FloatMatrix
{
public:

	static FloatMatrix zeroes( int m, int n );
	static FloatMatrix ones( int m, int n );

	FloatMatrix(); // makes a 0x0 matrix
	FloatMatrix( int nRows, int nCols, float fillValue = 0.f );
	FloatMatrix( const FloatMatrix& m );
	FloatMatrix( FloatMatrix&& m ); // move constructor
	FloatMatrix( FloatMatrix* m );
	virtual ~FloatMatrix();
	FloatMatrix& operator = ( const FloatMatrix& m );
	FloatMatrix& operator = ( FloatMatrix&& m ); // move assignment operator

	bool isNull() const; // a matrix is null if either nRows or nCols is 0

	void fill( float d );

	int numRows() const;
	int numCols() const;
	int numElements() const;
	Vector2i indexToSubscript( int idx ) const;
	int subscriptToIndex( int i, int j ) const;

	void resize( int nRows, int nCols );

	// returns false if nRows * nCols != numElements()
	bool reshape( int nRows, int nCols );

	// access at ( i, j )
	float& operator () ( int i, int j );
	const float& operator () ( int i, int j ) const;

	// access element k with column-major ordering
	float& operator [] ( int k );
	const float& operator [] ( int k ) const;

	// assigns m to this
	// this is resized if necessary
	void copy( const FloatMatrix& m );

	// assign a submatrix of m
	// starting at (i0,j0)
	// with size (nRows,nCols)
	//
	// to a submatrix of this
	// starting at (i1,j1)
	//
	// nRows = 0 means i1 to end
	// nCols = 0 means j1 to end
	void assign( const FloatMatrix& m, int i0 = 0, int j0 = 0, int i1 = 0, int j1 = 0, int nRows = 0, int nCols = 0 );

	float* data();
	const float* constData() const;

	// Returns the inverse of this
	// using LU factorization
	FloatMatrix inverted() const;
	void inverse( FloatMatrix& inv ) const;

	void transpose( FloatMatrix& t ) const;
	FloatMatrix transposed() const;

	// Returns the sum of the square of each element
	//float frobeniusNormSquared() const;

	FloatMatrix& operator += ( const FloatMatrix& x );
	FloatMatrix& operator -= ( const FloatMatrix& x );

	// Returns the dot product of a . b
	// a can be m x 1 or 1 x m
	// b can be m x 1 or 1 x m
	// a and b must be of the same length
	static float dot( const FloatMatrix& a, const FloatMatrix& b );

	// TODO: call scaledMultiplyAdd
	static void add( const FloatMatrix& a, const FloatMatrix& b, FloatMatrix& c );
	// TODO: call scaledMultiplyAdd
	static void subtract( const FloatMatrix& a, const FloatMatrix& b, FloatMatrix& c );

	// y <-- alpha * x + y
	static void scaledMultiplyAdd( float alpha, const FloatMatrix& x, FloatMatrix& y );

	// TODO: call sgemm
	static void multiply( const FloatMatrix& a, const FloatMatrix& b, FloatMatrix& c );	

	/*
	void eigenvalueDecomposition( QVector< QVector< float > >* eigen_vector,
		QVector< float >* eigen_value );
	static void homography( QVector< Vector3f > from,
		QVector< Vector3f > to, FloatMatrix& output );
	*/

	float minimum() const;
	float maximum() const;

	void print( const char* prefix = nullptr, const char* suffix = nullptr );
	QString toString();	

private:

	int m_nRows;
	int m_nCols;
	std::vector< float > m_data;

};

FloatMatrix operator + ( const FloatMatrix& a, const FloatMatrix& b );
FloatMatrix operator - ( const FloatMatrix& a, const FloatMatrix& b );
FloatMatrix operator - ( const FloatMatrix& a );

FloatMatrix operator * ( const FloatMatrix& a, const FloatMatrix& b );
