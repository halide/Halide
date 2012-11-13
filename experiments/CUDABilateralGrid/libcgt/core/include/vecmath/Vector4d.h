#ifndef VECTOR_4D_H
#define VECTOR_4D_H

class Vector2d;
class Vector3d;

class Vector4d
{
public:	

	Vector4d();
	Vector4d( double fx, double fy, double fz, double fw );
	Vector4d( const Vector2d& xy, double z, double w );
	Vector4d( double x, const Vector2d& yz, double w );
	Vector4d( double x, double y, const Vector2d& zw );
	Vector4d( const Vector2d& xy, const Vector2d& zw );
	Vector4d( const Vector3d& xyz, double w );
	Vector4d( double x, const Vector3d& yzw );

	Vector4d( const Vector4d& rv ); // copy constructor	
	Vector4d& operator = ( const Vector4d& rv ); // assignment operator
	// no destructor necessary

	// returns the ith element
	const double& operator [] ( int i ) const;
	double& operator [] ( int i );
	
	Vector2d xy() const;
	Vector2d yz() const;
	Vector2d zw() const;
	Vector2d wx() const;
	// TODO: the other combinations

	Vector3d xyz() const;
	Vector3d yzw() const;
	Vector3d zwx() const;
	Vector3d wxy() const;

	Vector3d xyw() const;
	Vector3d yzx() const;
	Vector3d zwy() const;
	Vector3d wxz() const;
	// TODO: the rest of the vec3 combinations

	// TODO: swizzle all the vec4s

	double abs() const;
	double absSquared() const;
	void normalize();
	Vector4d normalized() const;

	// if v.z != 0, v = v / v.w
	void homogenize();
	Vector4d homogenized() const;

	void negate();

	// ---- Utility ----
	operator const double* (); // automatic type conversion for GL
	void print() const;

	static double dot( const Vector4d& v0, const Vector4d& v1 );
	static Vector4d lerp( const Vector4d& v0, const Vector4d& v1, double alpha );

	union
	{
		struct
		{
			double x;
			double y;
			double z;
			double w;
		};
		double m_elements[ 4 ];
	};

};

Vector4d operator + ( const Vector4d& v0, const Vector4d& v1 );
Vector4d operator - ( const Vector4d& v0, const Vector4d& v1 );
Vector4d operator * ( const Vector4d& v0, const Vector4d& v1 );
Vector4d operator / ( const Vector4d& v0, const Vector4d& v1 );

Vector4d operator - ( const Vector4d& v );
Vector4d operator * ( double d, const Vector4d& v );
Vector4d operator * ( const Vector4d& v, double d );

#endif // VECTOR_4D_H
