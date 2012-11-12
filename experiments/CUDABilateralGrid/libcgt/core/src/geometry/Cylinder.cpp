#include "geometry/Cylinder.h"

#include "math/MathUtils.h"

Cylinder::Cylinder( float _radius, float _height, const Vector3f& _baseCenter ) :

	radius( _radius ),
	height( _height ),
	baseCenter( _baseCenter )

{

}

void Cylinder::tesselate( int nTheta, int nHeight,
	std::vector< Vector4f >& positions,
	std::vector< Vector3f >& normals )
{
	positions.clear();
	normals.clear();

	positions.reserve( 6 * nTheta * nHeight );
	normals.reserve( 6 * nTheta * nHeight );

	float dt = MathUtils::TWO_PI / nTheta;
	float dh = height / nHeight;

	Vector4f bc( baseCenter, 0 );

	int k = 0;
	for( int t = 0; t < nTheta; ++t )
	{
		float t0 = t * dt;
		float t1 = t0 + dt;

		for( int h = 0; h < nHeight; ++h )
		{
			float h0 = h * dh;
			float h1 = h0 + dh;

			float x00 = cosf( t0 );
			float y00 = sinf( t0 );
			float x10 = cosf( t1 );
			float y10 = sinf( t1 );
			float x01 = cosf( t0 );
			float y01 = sinf( t0 );
			float x11 = cosf( t1 );
			float y11 = sinf( t1 );

			Vector4f v00 = bc + Vector4f( radius * x00, radius * y00, h0, 1 );
			Vector3f n00( x00, y00, 0 );

			Vector4f v10 = bc + Vector4f( radius * x10, radius * y10, h0, 1 );
			Vector3f n10( x10, y10, 0 );

			Vector4f v01 = bc + Vector4f( radius * x01, radius * y01, h1, 1 );
			Vector3f n01( x01, y01, 0 );

			Vector4f v11 = bc + Vector4f( radius * x11, radius * y11, h1, 1 );
			Vector3f n11( x11, y11, 0 );

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

void Cylinder::sample( float thetaFraction, float zFraction, Vector4f& position, Vector3f& normal )
{
	// TODO: make tesselate use this?
	float theta = thetaFraction * MathUtils::TWO_PI;
	float z = zFraction * height;

	float x = cosf( theta );
	float y = sinf( theta );

	position = Vector4f( baseCenter, 0 ) + Vector4f( radius * x, radius * y, z, 1 );
	normal = Vector3f( x, y, 0 );
}
