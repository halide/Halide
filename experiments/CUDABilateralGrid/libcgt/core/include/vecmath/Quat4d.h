#ifndef QUAT4D_H
#define QUAT4D_H

class Quat4f;
class Vector3d;
class Vector4d;

class Quat4d
{
public:

	Quat4d();

	// q = w + x * i + y * j + z * k
	Quat4d( double w, double x, double y, double z );

	Quat4d( const Quat4d& rq ); // copy constructor
	Quat4d( const Quat4f& rq );
	Quat4d& operator = ( const Quat4d& rq ); // assignment operator
	// no destructor necessary

	// returns a quaternion with 0 real part
	Quat4d( const Vector3d& v );

	// copies the components of a Vector4d directly into this quaternion
	Quat4d( const Vector4d& v );

	// returns the ith element
	const double& operator [] ( int i ) const;
	double& operator [] ( int i );

	Vector3d xyz() const;
	Vector4d wxyz() const;

	double abs() const;
	double absSquared() const;
	void normalize();
	Quat4d normalized() const;

	void conjugate();
	Quat4d conjugated() const;

	void invert();
	Quat4d inverse() const;

	// returns unit vector for rotation and radians about the unit vector
	Vector3d getAxisAngle( double* radiansOut );

	// sets this quaternion to be a rotation of fRadians about v = < fx, fy, fz >, v need not necessarily be unit length
	void setAxisAngle( double radians, const Vector3d& axis );

	Vector3d rotateVector( const Vector3d& v );

	// ---- Utility ----
	void print();

	// quaternion dot product (a la vector)
	static double dot( const Quat4d& q0, const Quat4d& q1 );	

	// linear (stupid) interpolation
	static Quat4d lerp( const Quat4d& q0, const Quat4d& q1, double alpha );

	// TODO: http://number-none.com/product/Understanding%20Slerp,%20Then%20Not%20Using%20It/
	// look at the other ones - nlerp and log-quaternion lerp

	// TODO: epsilon
	static Quat4d slerp( const Quat4d& q0, const Quat4d& q1, double alpha, double cosOmegaThreshold = 0.9995 );

	// returns a unit quaternion that's a uniformly distributed rotation
	// given u[i] is a uniformly distributed random number in [0,1]
	// taken from Graphics Gems II
	static Quat4d randomRotation( double u0, double u1, double u2 );

#if 0
	// rotates pvVector by the rotation in pqRotation
	static void rotateVector( Quat4d* pqRotation, Vector3d* pvVector, Vector3d* pvOut );
#endif

	union
	{
		struct
		{
			double w;
			double x;
			double y;
			double z;
		};
		double m_elements[ 4 ];
	};

};

Quat4d operator + ( const Quat4d& q0, const Quat4d& q1 );
Quat4d operator - ( const Quat4d& q0, const Quat4d& q1 );
Quat4d operator * ( const Quat4d& q0, const Quat4d& q1 );
Quat4d operator * ( double d, const Quat4d& q );
Quat4d operator * ( const Quat4d& q, double d );

#endif // QUAT4D_H
