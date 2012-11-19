#include "geometry/Cone.h"

#include "math/MathUtils.h"

Cone::Cone( float _baseRadius, float _height, const Vector3f& _baseCenter ) :

	baseRadius( _baseRadius ),
	height( _height ),
	baseCenter( _baseCenter )

{

}

void Cone::tesselate( int nTheta, int nHeight,
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

			float r0 = baseRadius * ( 1 - h0 / height );
			float r1 = baseRadius * ( 1 - h1 / height );

			float x00 = r0 * cosf( t0 );
			float y00 = r0 * sinf( t0 );
			float x10 = r0 * cosf( t1 );
			float y10 = r0 * sinf( t1 );
			float x01 = r1 * cosf( t0 );
			float y01 = r1 * sinf( t0 );
			float x11 = r1 * cosf( t1 );
			float y11 = r1 * sinf( t1 );

			Vector4f v00 = bc + Vector4f( x00, y00, h0, 1 );
			Vector3f n00( x00, y00, r0 / sqrtf( r0 * r0 + h0 * h0 ) );
			n00.normalize();

			Vector4f v10 = bc + Vector4f( x10, y10, h0, 1 );
			Vector3f n10( x10, y10, r0 / sqrtf( r0 * r0 + h0 * h0 ) );
			n10.normalize();

			Vector4f v01 = bc + Vector4f( x01, y01, h1, 1 );
			Vector3f n01( x01, y01, r1 / sqrtf( r1 * r1 + h1 * h1 ) );
			n01.normalize();

			Vector4f v11 = bc + Vector4f( x11, y11, h1, 1 );
			Vector3f n11( x11, y11, r1 / sqrtf( r1 * r1 + h1 * h1 ) );
			n11.normalize();

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

void Cone::sample( float thetaFraction, float zFraction, Vector4f& position, Vector3f& normal )
{
	// TODO: make tesselate use this?
	float theta = thetaFraction * MathUtils::TWO_PI;
	float z = zFraction * height;

	float r = ( 1 - zFraction ) * baseRadius;

	float x = r * cosf( theta );
	float y = r * sinf( theta );

	position = Vector4f( baseCenter, 0 ) + Vector4f( x, y, z, 1 );
	normal = Vector3f( x, y, r / sqrtf( r * r + height * height ) );
	normal.normalize();
}
