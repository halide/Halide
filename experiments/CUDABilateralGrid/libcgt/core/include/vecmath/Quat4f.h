#ifndef QUAT4F_H
#define QUAT4F_H

class Vector3f;
class Vector4f;

#include "Matrix3f.h"

class Quat4f
{
public:

	static const Quat4f ZERO;
	static const Quat4f IDENTITY;

	Quat4f();

	// q = w + x * i + y * j + z * k
	Quat4f( float w, float x, float y, float z );
		
	Quat4f( const Quat4f& rq ); // copy constructor
	Quat4f& operator = ( const Quat4f& rq ); // assignment operator
	// no destructor necessary

	// returns a quaternion with 0 real part
	Quat4f( const Vector3f& v );

	// copies the components of a Vector4f directly into this quaternion
	Quat4f( const Vector4f& v );

	// returns the ith element
	const float& operator [] ( int i ) const;
	float& operator [] ( int i );

	Vector3f xyz() const;
	Vector4f wxyz() const;

	float abs() const;
	float absSquared() const;
	void normalize();
	Quat4f normalized() const;

	void conjugate();
	Quat4f conjugated() const;

	void invert();
	Quat4f inverse() const;

	// log and exponential maps
	Quat4f log() const;
	Quat4f exp() const;
	
	// returns unit vector for rotation and radians about the unit vector
	Vector3f getAxisAngle( float* radiansOut );

	// sets this quaternion to be a rotation of fRadians about v = < fx, fy, fz >, v need not necessarily be unit length
	void setAxisAngle( float radians, const Vector3f& axis );

	Vector3f rotateVector( const Vector3f& v );

	// ---- Utility ----
	void print();
 
	 // quaternion dot product (a la vector)
	static float dot( const Quat4f& q0, const Quat4f& q1 );	
	
	// linear (stupid) interpolation
	static Quat4f lerp( const Quat4f& q0, const Quat4f& q1, float alpha );

	// spherical linear interpolation
	static Quat4f slerp( const Quat4f& a, const Quat4f& b, float t, bool allowFlip = true );
	
	// spherical quadratic interoplation between a and b at point t
	// given quaternion tangents tanA and tanB (can be computed using squadTangent)	
	static Quat4f squad( const Quat4f& a, const Quat4f& tanA, const Quat4f& tanB, const Quat4f& b, float t );

	static Quat4f cubicInterpolate( const Quat4f& q0, const Quat4f& q1, const Quat4f& q2, const Quat4f& q3, float t );

	// Log-difference between a and b, used for squadTangent
	// returns log( a^-1 b )	
	static Quat4f logDifference( const Quat4f& a, const Quat4f& b );

	// Computes a tangent at center, defined by the before and after quaternions
	// Useful for squad()
	static Quat4f squadTangent( const Quat4f& before, const Quat4f& center, const Quat4f& after );		

	static Quat4f fromRotationMatrix( const Matrix3f& m );

	static Quat4f fromRotatedBasis( const Vector3f& x, const Vector3f& y, const Vector3f& z );

	// returns a unit quaternion that's a uniformly distributed rotation
	// given u[i] is a uniformly distributed random number in [0,1]
	// taken from Graphics Gems II
	static Quat4f randomRotation( float u0, float u1, float u2 );

#if 0
	// rotates pvVector by the rotation in pqRotation
	static void rotateVector( Quat4f* pqRotation, Vector3f* pvVector, Vector3f* pvOut );
#endif

	union
	{
		struct
		{
			float w;
			float x;
			float y;
			float z;
		};
		float m_elements[ 4 ];
	};

};

Quat4f operator + ( const Quat4f& q0, const Quat4f& q1 );
Quat4f operator - ( const Quat4f& q0, const Quat4f& q1 );
Quat4f operator * ( const Quat4f& q0, const Quat4f& q1 );
Quat4f operator * ( float f, const Quat4f& q );
Quat4f operator * ( const Quat4f& q, float f );

#endif // QUAT4F_H
