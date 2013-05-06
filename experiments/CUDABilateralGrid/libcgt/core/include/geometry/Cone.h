#pragma once

#include <vector>

#include "vecmath/Vector2f.h"
#include "vecmath/Vector3f.h"
#include "vecmath/Vector4f.h"

class Cone
{
public:

	Cone( float _baseRadius = 1, float _height = 1, const Vector3f& _baseCenter = Vector3f( 0, 0, 0 ) );

	void tesselate( int nTheta, int nHeight,
		std::vector< Vector4f >& positions,
		std::vector< Vector3f >& normals );

	// thetaFraction between 0 and 1
	// zFraction between 0 and 1
	void sample( float thetaFraction, float zFraction, Vector4f& position, Vector3f& normal );

	float baseRadius;
	float height;
	Vector3f baseCenter;

};
