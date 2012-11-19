#ifndef VECTOR_3D_H
#define VECTOR_3D_H

class Vector2d;
class Vector3f;

class Vector3d
{
public:

	Vector3d();
	Vector3d( double fx, double fy, double fz );
	Vector3d( const Vector2d& xy, double z );
	Vector3d( double x, const Vector2d& yz );

	Vector3d( const Vector3d& rv ); // copy constructor
	Vector3d( const Vector3f& rv );
	Vector3d& operator = ( const Vector3d& rv ); // assignment operator
	// no destructor necessary

	// returns the ith element
	const double& operator [] ( int i ) const;
	double& operator [] ( int i );
	
	Vector2d xy() const;
	Vector2d xz() const;
	Vector2d yz() const;
	// TODO: all the other combinations

	Vector3d xyz() const;
	Vector3d yzx() const;
	Vector3d zxy() const;
	// TODO: all the other combinations

	double abs() const;
	double absSquared() const;
	void normalize();
	Vector3d normalized() const;

	void negate();

	// ---- Utility ----
	operator const double* (); // automatic type conversion for GL
	void print() const;

	static double dot( const Vector3d& v0, const Vector3d& v1 );	

	static Vector3d cross( const Vector3d& v0, const Vector3d& v1 );

	// returns v0 * ( 1 - alpha ) * v1 * alpha
	static Vector3d lerp( const Vector3d& v0, const Vector3d& v1, double alpha );

	union
	{
		struct
		{
			double x;
			double y;
			double z;
		};
		double m_elements[3];
	};

};

Vector3d operator + ( const Vector3d& v0, const Vector3d& v1 );
Vector3d operator - ( const Vector3d& v0, const Vector3d& v1 );
Vector3d operator * ( const Vector3d& v0, const Vector3d& v1 );
Vector3d operator / ( const Vector3d& v0, const Vector3d& v1 );

Vector3d operator - ( const Vector3d& v );
Vector3d operator * ( double d, const Vector3d& v );
Vector3d operator * ( const Vector3d& v, double d );

#endif // VECTOR_3D_H
