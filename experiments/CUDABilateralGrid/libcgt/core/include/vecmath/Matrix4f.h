#pragma once

#include <cstdio>

class Matrix2f;
class Matrix3f;
class Quat4f;
class Vector3f;
class Vector4f;

// 4x4 Matrix, stored in column major order (OpenGL style)
class Matrix4f
{
public:

	// 4x4 zero matrix
	Matrix4f();
	Matrix4f( float m00, float m01, float m02, float m03,
		float m10, float m11, float m12, float m13,
		float m20, float m21, float m22, float m23,
		float m30, float m31, float m32, float m33 );
	
	// setColumns = true ==> sets the columns of the matrix to be [v0 v1 v2 v3]
	// otherwise, sets the rows
	Matrix4f( const Vector4f& v0, const Vector4f& v1, const Vector4f& v2, const Vector4f& v3, bool setColumns = true );
	
	Matrix4f( const Matrix4f& rm ); // copy constructor
	Matrix4f& operator = ( const Matrix4f& rm ); // assignment operator
	// no destructor necessary

	const float& operator () ( int i, int j ) const;
	float& operator () ( int i, int j );

	Vector4f getRow( int i ) const;
	void setRow( int i, const Vector4f& v );

	// get column j (mod 4)
	Vector4f getCol( int j ) const;
	void setCol( int j, const Vector4f& v );

	// gets the 2x2 submatrix of this matrix to m
	// starting with upper left corner at (i0, j0)
	Matrix2f getSubmatrix2x2( int i0, int j0 ) const;

	// gets the 3x3 submatrix of this matrix to m
	// starting with upper left corner at (i0, j0)
	Matrix3f getSubmatrix3x3( int i0, int j0 ) const;

	// sets a 2x2 submatrix of this matrix to m
	// starting with upper left corner at (i0, j0)
	void setSubmatrix2x2( int i0, int j0, const Matrix2f& m );

	// sets a 3x3 submatrix of this matrix to m
	// starting with upper left corner at (i0, j0)
	void setSubmatrix3x3( int i0, int j0, const Matrix3f& m );

	float determinant() const;
	Matrix4f inverse( bool* pbIsSingular = NULL, float epsilon = 0.f ) const; // TODO: invert(), inverted()?

	void transpose();
	Matrix4f transposed() const;

	// inverse transpose of top left 3x3 submatrix
	Matrix3f normalMatrix() const;
	
	// inverse transpose of top left 3x3 submatrix on top left
	// 0 elsewhere
	Matrix4f normalMatrix4x4() const;

	// implicit cast
	operator const float* () const;
	operator float* ();
	void print();

	static Matrix4f ones();
	static Matrix4f identity();
	static Matrix4f translation( float x, float y, float z );
	static Matrix4f translation( const Vector3f& rTranslation );
	static Matrix4f rotateX( float radians );
	static Matrix4f rotateY( float radians );
	static Matrix4f rotateZ( float radians );
	static Matrix4f rotation( const Vector3f& rDirection, float degrees );
	static Matrix4f scaling( float sx, float sy, float sz );
	static Matrix4f uniformScaling( float s );
	static Matrix4f lookAt( const Vector3f& eye, const Vector3f& center, const Vector3f& up );
	static Matrix4f orthographicProjection( float width, float height, float zNear, float zFar, bool directX );
	static Matrix4f orthographicProjection( float left, float right, float bottom, float top, float zNear, float zFar, bool directX );
	static Matrix4f perspectiveProjection( float fLeft, float fRight, float fBottom, float fTop, float fZNear, float fZFar, bool directX );
	static Matrix4f perspectiveProjection( float fovYRadians, float aspect, float zNear, float zFar, bool directX );
	static Matrix4f infinitePerspectiveProjection( float fLeft, float fRight, float fBottom, float fTop, float fZNear, bool directX );

	// Returns the rotation matrix represented by a quaternion
	// uses a normalized version of q
	static Matrix4f rotation( const Quat4f& q );

	// returns an orthogonal matrix that's a uniformly distributed rotation
	// given u[i] is a uniformly distributed random number in [0,1]
	static Matrix4f randomRotation( float u0, float u1, float u2 );

	union
	{
		struct
		{
			float m00;
			float m10;
			float m20;
			float m30;

			float m01;
			float m11;
			float m21;
			float m31;

			float m02;
			float m12;
			float m22;
			float m32;

			float m03;
			float m13;
			float m23;
			float m33;
		};
		float m_elements[ 16 ];
	};

};

// Matrix-Vector multiplication
// 4x4 * 4x1 ==> 4x1
Vector4f operator * ( const Matrix4f& m, const Vector4f& v );

// Matrix-Matrix multiplication
Matrix4f operator * ( const Matrix4f& x, const Matrix4f& y );
