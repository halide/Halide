#ifndef SPHERE_H
#define SPHERE_H

#include <vector>

#include "vecmath/Vector2f.h"
#include "vecmath/Vector3f.h"
#include "vecmath/Vector4f.h"

class Sphere
{
public:

	Sphere( float _radius = 1, const Vector3f& _center = Vector3f( 0, 0, 0 ) );

	// HACK: fix the stupid redundancy
	void tesselate( int nTheta, int nPhi,
		std::vector< Vector3f >& positions,
		std::vector< Vector3f >& normals );

	void tesselate( int nTheta, int nPhi,
		std::vector< Vector4f >& positions,
		std::vector< Vector3f >& normals );

	Vector3f center;
	float radius;

};

#endif // SPHERE_H
