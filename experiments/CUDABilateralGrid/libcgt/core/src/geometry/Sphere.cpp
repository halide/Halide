#include "geometry/Sphere.h"

#include <cmath>

#include "math/MathUtils.h"

Sphere::Sphere( float _radius, const Vector3f& _center ) :

	radius( _radius ),
	center( _center )

{

}

void Sphere::tesselate( int nTheta, int nPhi,
	std::vector< Vector3f >& positions,
	std::vector< Vector3f >& normals )
{
	positions.clear();
	normals.clear();

	positions.reserve( 6 * nTheta * nPhi );
	normals.reserve( 6 * nTheta * nPhi );

	float dt = MathUtils::TWO_PI / nTheta;
	float dp = MathUtils::PI / nPhi;

	Vector3f c( center );

	for( int t = 0; t < nTheta; ++t )
	{
		float t0 = t * dt;
		float t1 = t0 + dt;

		for( int p = 0; p < nPhi; ++p )
		{
			float p0 = p * dp;
			float p1 = p0 + dp;

			float x00 = cosf( t0 ) * sinf( p0 );
			float y00 = sinf( t0 ) * sinf( p0 );
			float z00 = cosf( p0 );
			float x10 = cosf( t1 ) * sinf( p0 );
			float y10 = sinf( t1 ) * sinf( p0 );
			float z10 = cosf( p0 );
			float x01 = cosf( t0 ) * sinf( p1 );
			float y01 = sinf( t0 ) * sinf( p1 );
			float z01 = cosf( p1 );
			float x11 = cosf( t1 ) * sinf( p1 );
			float y11 = sinf( t1 ) * sinf( p1 );
			float z11 = cosf( p1 );

			Vector3f v00 = c + Vector3f( radius * x00, radius * y00, radius * z00 );
			Vector3f n00( x00, y00, z00 );
			Vector3f v10 = c + Vector3f( radius * x10, radius * y10, radius * z10 );
			Vector3f n10( x10, y10, z10 );
			Vector3f v01 = c + Vector3f( radius * x01, radius * y01, radius * z01 );
			Vector3f n01( x01, y01, z01 );
			Vector3f v11 = c + Vector3f( radius * x11, radius * y11, radius * z11 );
			Vector3f n11( x11, y11, z11 );

			positions.push_back( v00 );
			normals.push_back( n00 );
			positions.push_back( v10 );
			normals.push_back( n10 );
			positions.push_back( v01 );
			normals.push_back( n01 );

			positions.push_back( v01 );
			normals.push_back( n01 );
			positions.push_back( v10 );
			normals.push_back( n10 );
			positions.push_back( v11 );
			normals.push_back( n11 );
		}
	}
}

void Sphere::tesselate( int nTheta, int nPhi,
	std::vector< Vector4f >& positions,
	std::vector< Vector3f >& normals )
{
	positions.clear();
	normals.clear();

	positions.reserve( 6 * nTheta * nPhi );
	normals.reserve( 6 * nTheta * nPhi );

	float dt = MathUtils::TWO_PI / nTheta;
	float dp = MathUtils::PI / nPhi;

	Vector4f c( center, 0 );

	for( int t = 0; t < nTheta; ++t )
	{
		float t0 = t * dt;
		float t1 = t0 + dt;

		for( int p = 0; p < nPhi; ++p )
		{
			float p0 = p * dp;
			float p1 = p0 + dp;

			float x00 = cosf( t0 ) * sinf( p0 );
			float y00 = sinf( t0 ) * sinf( p0 );
			float z00 = cosf( p0 );
			float x10 = cosf( t1 ) * sinf( p0 );
			float y10 = sinf( t1 ) * sinf( p0 );
			float z10 = cosf( p0 );
			float x01 = cosf( t0 ) * sinf( p1 );
			float y01 = sinf( t0 ) * sinf( p1 );
			float z01 = cosf( p1 );
			float x11 = cosf( t1 ) * sinf( p1 );
			float y11 = sinf( t1 ) * sinf( p1 );
			float z11 = cosf( p1 );

			Vector4f v00 = c + Vector4f( radius * x00, radius * y00, radius * z00, 1 );
			Vector3f n00( x00, y00, z00 );
			Vector4f v10 = c + Vector4f( radius * x10, radius * y10, radius * z10, 1 );
			Vector3f n10( x10, y10, z10 );
			Vector4f v01 = c + Vector4f( radius * x01, radius * y01, radius * z01, 1 );
			Vector3f n01( x01, y01, z01 );
			Vector4f v11 = c + Vector4f( radius * x11, radius * y11, radius * z11, 1 );
			Vector3f n11( x11, y11, z11 );

			positions.push_back( v00 );
			normals.push_back( n00 );
			positions.push_back( v10 );
			normals.push_back( n10 );
			positions.push_back( v01 );
			normals.push_back( n01 );

			positions.push_back( v01 );
			normals.push_back( n01 );
			positions.push_back( v10 );
			normals.push_back( n10 );
			positions.push_back( v11 );
			normals.push_back( n11 );
		}
	}
}
