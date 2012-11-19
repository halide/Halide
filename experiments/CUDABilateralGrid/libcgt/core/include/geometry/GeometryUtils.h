#ifndef GEOMETRY_UTILS_H
#define GEOMETRY_UTILS_H

class Vector3f;

#include <QVector>

#include "math/Random.h"
#include "vecmath/Matrix3f.h"
#include "vecmath/Vector2i.h"
#include "vecmath/Vector4f.h"
#include "geometry/BoundingBox2f.h"
#include "geometry/BoundingBox3f.h"

class GeometryUtils
{
public:

	// TODO: static const float epsilon
	// have most functions take in an optional epsilon
	static float EPSILON;

	static BoundingBox2f triangleBoundingBox( const Vector2f& v0, const Vector2f& v1, const Vector2f& v2 );

	static Vector2f triangleCentroid( const Vector2f& v0, const Vector2f& v1, const Vector2f& v2 );

	// pixels are centered at half-integer coordinates
	static QVector< Vector2f > pixelsInTriangle( const Vector2f& v0, const Vector2f& v1, const Vector2f& v2 );

	// given normal n, origin p, and point to be tested s
	// return n dot ( s - p )
	static float edgeTest( const Vector2f& edgeNormal, const Vector2f& edgeOrigin, const Vector2f& point );

	// given normal n, origin p, and point to be tested s
	// returns the *conservative* edge test of s
	static float edgeTestConservative( const Vector2f& edgeNormal, const Vector2f& edgeOrigin, const Vector2f& point );

	// conservative rasterization
	// v0, v1, v2 must be counterclockwise oriented
	// pixel centers are at half-integer coordinates
	static QVector< Vector2f > pixelsInTriangleConservative( const Vector2f& v0, const Vector2f& v1, const Vector2f& v2 );

	// returns true if two points (p0, p1) are on the same side of
	// the line determined by (v0, v1)
	static bool pointsOnSameSide( const Vector2f& p0, const Vector2f& p1,
		const Vector2f& v0, const Vector2f& v1 );

	// TODO: just use barycentrics and return them optionally?
	static bool pointInTriangle( const Vector2f& point,
		const Vector2f& v0, const Vector2f& v1, const Vector2f& v2 );

	// given a point p inside a triangle and the triangle's 3 vertices v_i
	// returns its barycentric coordinates b = [ l0, l1, l2 ]
	// p = l0 * v0 + l1 * v1 + l2 * v2, where l2 = 1 - l0 - l1
	static Vector3f euclideanToBarycentric( const Vector2f& p,
		const Vector2f& v0, const Vector2f& v1, const Vector2f& v2 );

	// given barycentric coordinates b = [ b0, b1, b2 ]
	// returns its euclidean coordinates p = b0 * v0 + b1 * v1 + b2 * v2
	static Vector2f barycentricToEuclidean( const Vector3f& b,
		const Vector2f& v0, const Vector2f& v1, const Vector2f& v2 );

	// given barycentric coordinates b = [ b0, b1, b2 ]
	// returns its euclidean coordinates p = b0 * v0 + b1 * v1 + b2 * v2
	static Vector3f barycentricToEuclidean( const Vector3f& b,
		const Vector3f& v0, const Vector3f& v1, const Vector3f& v2 );

    // given a vector n, writes two unit vectors normal to n and to each other to b1 and b2
    static void getBasis( const Vector3f& n, Vector3f* b1, Vector3f* b2 );

	// given a non-zero vector z, returns a right handed basis matrix [ x y z' ]
	// such that:
	//    z' = z / ||z||
	//    x, y, and z' are all unit vectors and x cross y = z'
	static Matrix3f getRightHandedBasis( const Vector3f& z );

	// given a vector z, and a preferred up vector y,
	// writes two unit vectors normal to z and to each other to b1 and b2
	static void getBasisWithPreferredUp( const Vector3f& z, const Vector3f& preferredY,
		Vector3f* b1, Vector3f* b2 );

	static bool pointInBox( const Vector3f& crPoint, const BoundingBox3f& bbox );
	
	static bool pointInsideSphere( const Vector3f& crPoint,
		const Vector3f& crSphereCenter, float sphereRadius );

	// given a line segment v0 --> v1
	// and a point p
	// finds the point on the segment closest to p
	static Vector2f closestPointOnSegment( const Vector2f& p, const Vector2f& v0, const Vector2f& v1 );
	static Vector3f closestPointOnSegment( const Vector3f& p, const Vector3f& v0, const Vector3f& v1 );

	static Vector2f closestPointOnTriangle( const Vector2f& p, const Vector2f& v0, const Vector2f& v1, const Vector2f& v2 );

    //dir1 and dir2 should be normalized
    static bool rayRayIntersection( const Vector2f& p1, const Vector2f& dir1,
                                    const Vector2f& p2, const Vector2f& dir2, Vector2f &outIntersection);

    //dir should be normalized
    static bool lineLineSegmentIntersection( const Vector2f& p, const Vector2f& dir,
                                             const Vector2f& p1, const Vector2f& p2, Vector2f &outIntersection);

	// plane is defined by dot( plane.xyz, X ) = plane.w
	static bool rayPlaneIntersection( const Vector3f& crRayOrigin, const Vector3f& crRayDirection,
		const Vector4f& crPlane,
		Vector3f& rIntersectionPoint );

	// ray triangle intersection
	// returns true if the ray intersects the triangle
	// TODO: one sided intersection is also in there, ifdefed
	static bool rayTriangleIntersection( const Vector3f& crRayOrigin, const Vector3f& crRayDirection,
		const Vector3f& crV0, const Vector3f& crV1, const Vector3f& crV2,
		float* t, float* u, float* v );

	static bool triangleAABBOverlap( Vector3f* pv0, Vector3f* pv1, Vector3f* pv2,
		BoundingBox3f* pBox );

	// given u along edge 0 -> 1 and v along edge 0 -> 2
	// and the interpolants on the vertices
	// returns the interpolated value
	static float triangleInterpolation( float interpolant0, float interpolant1, float interpolant2,
		float u, float v );

	// plane is defined by dot( plane.xyz, X ) = plane.w
	static float pointToPlaneDistance( const Vector3f& point, const Vector4f& plane );

    // lineDir should be normalized
	static float pointToLineDistance( const Vector3f& point, const Vector3f& linePoint, const Vector3f &lineDir );

    // lineDirs don't have to be normalized
	static float lineToLineDistance( const Vector3f& linePoint1, const Vector3f& lineDir1, const Vector3f& linePoint2, const Vector3f& lineDir2 );

#if 0
	// TODO: make this non-nasty
	static void tripleSphereIntersection( Vector3f* c0, float r0,
		Vector3f* c1, float r1,
		Vector3f* c2, float r2,
		int* numIntersections,
		Vector3f* intersect0,
		Vector3f* intersect1 );
#endif

	static Vector3f randomPointInSphere( float radius, Random& random );

	static std::vector< Vector2f > uniformSampleLineSegment( const Vector2f& p0, const Vector2f& p1, int nSamples );
	
	static std::vector< Vector3f > uniformSampleLineSegment( const Vector3f& p0, const Vector3f& p1, int nSamples );

	static std::vector< Vector2f > uniformSampleBoxAroundLineSegment( const Vector2f& p0, const Vector2f& p1,
		float width, int nSamplesWidth, int nSamplesLength );

private:

};

#endif
