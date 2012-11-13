#ifndef MATRIX2D_H
#define MATRIX2D_H

#include <cstdio>

class Matrix2f;
class Vector2f;
class Vector2d;

// 2x2 Matrix, stored in column major order (OpenGL style)
class Matrix2d
{
public:

	Matrix2d();
	Matrix2d( double m00, double m01,
		double m10, double m11 );

	// setColumns = true ==> sets the columns of the matrix to be [v0 v1]
	// otherwise, sets the rows
	Matrix2d( const Vector2d& v0, const Vector2d& v1, bool setColumns = true );

	Matrix2d( const Matrix2d& rm ); // copy constructor
	Matrix2d( const Matrix2f& rm );
	Matrix2d& operator = ( const Matrix2d& rm ); // assignment operator
	// no destructor necessary

	const double& operator () ( int i, int j ) const;
	double& operator () ( int i, int j );

	Vector2d getRow( int i ) const;
	void setRow( int i, const Vector2d& v );

	Vector2d getCol( int j ) const;
	void setCol( int j, const Vector2d& v );

	double determinant();
	Matrix2d inverse( bool* pbIsSingular = NULL, double epsilon = 0.0 ); // TODO: in place inverse

	// TODO: transpose, transposed()

	// ---- Utility ----
	operator double* (); // automatic type conversion for GL
	void print(); // TODO: toString()?

	static double determinant2x2( double m00, double m01,
		double m10, double m11 );

	static Matrix2d ones();
	static Matrix2d identity();
	static Matrix2d rotation( double degrees );

	union
	{
		struct
		{
			double m00;
			double m10;
			double m01;
			double m11;
		};
		double m_elements[ 4 ];
	};

};

// Matrix-Vector multiplication
// 2x2 * 2x1 ==> 2x1
Vector2f operator * ( const Matrix2d& m, const Vector2d& v );

// Matrix-Matrix multiplication
Matrix2d operator * ( const Matrix2d& x, const Matrix2d& y );

#endif // MATRIX2D_H
