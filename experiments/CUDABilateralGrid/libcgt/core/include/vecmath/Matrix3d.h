#ifndef MATRIX3D_H
#define MATRIX3D_H

#include <cstdio>

class Matrix2d;
class Matrix3f;
class Quat4f;
class Vector3d;
class Vector3d;

// 3x3 Matrix, stored in column major order (OpenGL style)
class Matrix3d
{
public:

	Matrix3d();
	Matrix3d( double m00, double m01, double m02,
		double m10, double m11, double m12,
		double m20, double m21, double m22 );

	// setColumns = true ==> sets the columns of the matrix to be [v0 v1 v2]
	// otherwise, sets the rows
	Matrix3d( const Vector3d& v0, const Vector3d& v1, const Vector3d& v2, bool setColumns = true );

	Matrix3d( const Matrix3d& rm ); // copy constructor
	Matrix3d( const Matrix3f& rm ); // copy constructor

	Matrix3d& operator = ( const Matrix3d& rm ); // assignment operator
	// no destructor necessary

	const double& operator () ( int i, int j ) const;
	double& operator () ( int i, int j );

	Vector3d getRow( int i ) const;
	void setRow( int i, const Vector3d& v );

	Vector3d getCol( int j ) const;
	void setCol( int j, const Vector3d& v );

	// gets the 2x2 submatrix of this matrix to m
	// starting with upper left corner at (i0, j0)
	Matrix2d getSubmatrix2x2( int i0, int j0 ) const;

	// sets a 2x2 submatrix of this matrix to m
	// starting with upper left corner at (i0, j0)
	void setSubmatrix2x2( int i0, int j0, const Matrix2d& m );

	double determinant() const;
	Matrix3d inverse( bool* pbIsSingular = NULL, double epsilon = 0.0 ) const; // TODO: invert in place as well

	void transpose();
	Matrix3d transposed() const;

	// ---- Utility ----
	operator double* (); // automatic type conversion for GL
	void print();

	static double determinant3x3( double m00, double m01, double m02,
		double m10, double m11, double m12,
		double m20, double m21, double m22 );

	static Matrix3d ones();
	static Matrix3d identity();
	static Matrix3d rotation( const Vector3d& rDirection, double degrees );

	// Returns the rotation matrix represented by a unit quaternion
	// if q is not normalized, it it normalized first
	static Matrix3d rotation( const Quat4f& rq );

	union
	{
		struct
		{
			double m00;
			double m10;
			double m20;

			double m01;
			double m11;
			double m21;

			double m02;
			double m12;
			double m22;
		};
		double m_elements[ 9 ];
	};

};

// Matrix-Vector multiplication
// 3x3 * 3x1 ==> 3x1
Vector3d operator * ( const Matrix3d& m, const Vector3d& v );

// Matrix-Matrix multiplication
Matrix3d operator * ( const Matrix3d& x, const Matrix3d& y );

#endif // MATRIX3D_H
