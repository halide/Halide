#include "geometry/GeometryUtils.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "vecmath/Vector3f.h"
#include "math/Arithmetic.h"
#include "math/MathUtils.h"
#include "vecmath/Matrix2f.h"

using namespace std;

// static
float GeometryUtils::EPSILON = 0.0001f;

// static
BoundingBox2f GeometryUtils::triangleBoundingBox( const Vector2f& v0, const Vector2f& v1, const Vector2f& v2 )
{
	float minX = min( min( v0.x, v1.x ), v2.x );
	float minY = min( min( v0.y, v1.y ), v2.y );

	float maxX = max( max( v0.x, v1.x ), v2.x );
	float maxY = max( max( v0.y, v1.y ), v2.y );

	return BoundingBox2f( minX, minY, maxX, maxY );
}

// static
Vector2f GeometryUtils::triangleCentroid( const Vector2f& v0, const Vector2f& v1, const Vector2f& v2 )
{
	return ( 1.f / 3.f ) * ( v0 + v1 + v2 );
}

// static
QVector< Vector2f > GeometryUtils::pixelsInTriangle( const Vector2f& v0, const Vector2f& v1, const Vector2f& v2 )
{
	BoundingBox2f bbox = triangleBoundingBox( v0, v1, v2 );
	int xStart = Arithmetic::floorToInt( bbox.minimum().x );
	int xEnd = Arithmetic::ceilToInt( bbox.maximum().x );
	int yStart = Arithmetic::floorToInt( bbox.minimum().y );
	int yEnd = Arithmetic::ceilToInt( bbox.maximum().y );

	QVector< Vector2f > pointsInside;

	for( int y = yStart; y <= yEnd; ++y )
	{
		for( int x = xStart; x <= xEnd; ++x )
		{
			Vector2f p( x + 0.5f, y + 0.5f );

			if( pointInTriangle( p, v0, v1, v2 ) )
			{
				pointsInside.append( p );
			}
		}
	}

	return pointsInside;
}

// static
float GeometryUtils::edgeTest( const Vector2f& edgeNormal, const Vector2f& edgeOrigin, const Vector2f& point )
{
	return Vector2f::dot( edgeNormal, point - edgeOrigin );
}

// static
float GeometryUtils::edgeTestConservative( const Vector2f& edgeNormal, const Vector2f& edgeOrigin, const Vector2f& point )
{
	float tx = -0.5f;
	float ty = -0.5f;

	if( edgeNormal.x >= 0 )
	{
		tx = 0.5f;
	}
	if( edgeNormal.y >= 0 )
	{
		ty = 0.5f;
	}

	Vector2f offset( tx, ty );

	return Vector2f::dot( edgeNormal, point ) + edgeTest( edgeNormal, edgeOrigin, offset );
}

// static
QVector< Vector2f > GeometryUtils::pixelsInTriangleConservative( const Vector2f& v0, const Vector2f& v1, const Vector2f& v2 )
{
	// set up edges
	Vector2f edge01 = v1 - v0;
	Vector2f edge12 = v2 - v1;
	Vector2f edge20 = v0 - v2;

	// get edge normals
	Vector2f normal01 = edge01.normal();
	Vector2f normal12 = edge12.normal();
	Vector2f normal20 = edge20.normal();

	BoundingBox2f bbox = triangleBoundingBox( v0, v1, v2 );
	int xStart = Arithmetic::floorToInt( bbox.minimum().x );
	int xEnd = Arithmetic::ceilToInt( bbox.maximum().x );
	int yStart = Arithmetic::floorToInt( bbox.minimum().y );
	int yEnd = Arithmetic::ceilToInt( bbox.maximum().y );

	QVector< Vector2f > pointsInside;

	for( int y = yStart; y <= yEnd; ++y )
	{
		for( int x = xStart; x <= xEnd; ++x )
		{
			Vector2f p( x + 0.5f, y + 0.5f );

			float passed01 = edgeTestConservative( normal01, v0, p );
			float passed12 = edgeTestConservative( normal12, v1, p );
			float passed20 = edgeTestConservative( normal20, v2, p );

			if( ( passed01 > 0 ) &&
				( passed12 > 0 ) &&
				( passed20 > 0 ) )
			{
				pointsInside.append( p );
			}
		}
	}

	return pointsInside;
}

// static
bool GeometryUtils::pointsOnSameSide( const Vector2f& p0, const Vector2f& p1,
									 const Vector2f& v0, const Vector2f& v1 )
{
	Vector2f edge = v1 - v0;
	Vector2f d0 = p0 - v0;
	Vector2f d1 = p1 - v0;

	Vector3f crossProduct0 = Vector2f::cross( edge, d0 );
	Vector3f crossProduct1 = Vector2f::cross( edge, d1 );

	return( Vector3f::dot( crossProduct0, crossProduct1 ) >= 0 );
}

// static
bool GeometryUtils::pointInTriangle( const Vector2f& point,
									const Vector2f& v0, const Vector2f& v1, const Vector2f& v2 )
{
	bool edge0 = pointsOnSameSide( point, v0, v1, v2 );
	bool edge1 = pointsOnSameSide( point, v1, v2, v0 );
	bool edge2 = pointsOnSameSide( point, v2, v0, v1 );

	return( edge0 && edge1 && edge2 );
}

// static
Vector3f GeometryUtils::euclideanToBarycentric( const Vector2f& p,
											   const Vector2f& v0, const Vector2f& v1, const Vector2f& v2 )
{
	// r = l0 v0 + l1 v1 + l2 v2
	// this yields the linear system
	// [ ( x0 - x2 )	( x1 - x2 ) ] [ l0 ] = [ x - x2 ]
	// [ ( y0 - y2 )	( y1 - y2 ) ] [ l1 ]   [ y - y2 ]
	// a l = b

	Matrix2f a
	(
		v0.x - v2.x, v1.x - v2.x,
		v0.y - v2.y, v1.y - v2.y
	);

	Vector2f b
	(
		p.x - v2.x,
		p.y - v2.y
	);

	bool bSingular;
	Matrix2f ai = a.inverse( &bSingular );
	assert( !bSingular );

	Vector2f l0l1 = ai * b;
	return Vector3f( l0l1, 1 - l0l1.x - l0l1.y );
}

// static
Vector2f GeometryUtils::barycentricToEuclidean( const Vector3f& b,
											   const Vector2f& v0, const Vector2f& v1, const Vector2f& v2 )
{
	return( b.x * v0 + b.y * v1 + b.z * v2 );
}

// static
Vector3f GeometryUtils::barycentricToEuclidean( const Vector3f& b,
	const Vector3f& v0, const Vector3f& v1, const Vector3f& v2 )
{
	return( b.x * v0 + b.y * v1 + b.z * v2 );
}

// static
void GeometryUtils::getBasis( const Vector3f& n, Vector3f* b1, Vector3f* b2 )
{
    if( n.absSquared() < 1e-8f )
	{
        if( b1 != nullptr )
		{
            *b1 = Vector3f( 1, 0, 0 );
		}
        if( b2 != nullptr )
		{
            *b2 = Vector3f( 0, 1, 0 );
		}
        return;
    }
    
    Vector3f vec;

    if( fabs( n[0] ) <= fabs( n[1] ) && fabs( n[0] ) <= fabs( n[2] ) )
	{
        vec = Vector3f( 1, 0, 0 );
	}
    else if( fabs( n[1] ) <= fabs( n[2] ) )
	{
        vec = Vector3f( 0, 1, 0 );
	}
    else
	{
        vec = Vector3f( 0, 0, 1 );
	}
    
    vec = Vector3f::cross( n, vec ).normalized(); // first basis vector
    if( b1 != nullptr )
	{
        *b1 = vec;
	}
    vec = Vector3f::cross( n, vec ).normalized(); // second basis vector
    if( b2 != nullptr )
	{
        *b2 = vec;
	}
}

// static
Matrix3f GeometryUtils::getRightHandedBasis( const Vector3f& z )
{
	Vector3f x;
	Vector3f y;
	Vector3f z2 = z.normalized();

	getBasis( z2, &x, &y );

	Matrix3f m( x, y, z2 );

	if( m.determinant() < 0 )
	{
		return Matrix3f( y, x, z2 );
	}
	else
	{
		return m;
	}
}

// static
void GeometryUtils::getBasisWithPreferredUp( const Vector3f& z, const Vector3f& preferredY,
											Vector3f* b1, Vector3f* b2 )
{
	// TODO: make this robust:
	// check if z is not unit (or really tiny)
	// check if z isn't parallel to preferred y

	Vector3f z2 = z.normalized();
	Vector3f y2 = preferredY.normalized();

	Vector3f x = Vector3f::cross( z2, y2 );
	Vector3f y3 = Vector3f::cross( x, z2 );

	*b1 = x;
	*b2 = y3;
}

// static
bool GeometryUtils::pointInBox( const Vector3f& crPoint, const BoundingBox3f& bbox )
{
	float x = crPoint.x;
	float y = crPoint.y;
	float z = crPoint.z;

	return( ( x > bbox.minimum().x ) && ( x < bbox.maximum().x ) &&
		( y > bbox.minimum().y ) && ( y < bbox.maximum().y ) &&
		( z > bbox.minimum().z ) && ( z < bbox.maximum().z ) );
}

// static
bool GeometryUtils::pointInsideSphere( const Vector3f& crPoint,
									  const Vector3f& crSphereCenter, float sphereRadius )
{
	Vector3f diff = crPoint - crSphereCenter;
	return( diff.absSquared() < ( sphereRadius * sphereRadius ) );
}


// static
Vector2f GeometryUtils::closestPointOnSegment( const Vector2f& p, const Vector2f& v0, const Vector2f& v1 )
{
	Vector2f v01 = v1 - v0;
	float d = Vector2f::dot( v01, p - v0 );

	float t = d / v01.absSquared();
	if( t < 0.f )
	{
		t = 0.f;
	}
	else if( t > 1.f )
	{
		t = 1.f;
	}

	return v0 + t * v01;
}

// static
Vector3f GeometryUtils::closestPointOnSegment( const Vector3f& p, const Vector3f& v0, const Vector3f& v1 )
{
	Vector3f v01 = v1 - v0;
	float d = Vector3f::dot( v01, p - v0 );

	float t = d / v01.absSquared();
	if( t < 0.f )
	{
		t = 0.f;
	}
	else if( t > 1.f )
	{
		t = 1.f;
	}

	return v0 + t * v01;
}

// static
Vector2f GeometryUtils::closestPointOnTriangle( const Vector2f& p, const Vector2f& v0, const Vector2f& v1, const Vector2f& v2 )
{
	Vector2f closest0 = closestPointOnSegment( p, v0, v1 );
	Vector2f closest1 = closestPointOnSegment( p, v1, v2 );
	Vector2f closest2 = closestPointOnSegment( p, v2, v0 );

	float d0Squared = ( closest0 - p ).absSquared();
	float d1Squared = ( closest1 - p ).absSquared();
	float d2Squared = ( closest2 - p ).absSquared();

	// yuck

	if( d0Squared < d1Squared )
	{
		if( d0Squared < d2Squared )
		{
			return closest0;
		}
		else
		{
			return closest2;
		}
	}
	else
	{
		if( d2Squared < d1Squared )
		{
			return closest2;
		}
		else
		{
			return closest1;
		}
	}
}

//static
//dir1 and dir2 should be normalized
bool GeometryUtils::rayRayIntersection( const Vector2f& p1, const Vector2f& dir1,
                                        const Vector2f& p2, const Vector2f& dir2, Vector2f &outIntersection)
{
    Vector2f dir90(-dir1[1], dir1[0]);
    float dirCross = Vector2f::dot(dir2, dir90);
    if(fabs(dirCross) < EPSILON)
        return false;
    float param = Vector2f::dot(p1 - p2, dir90) / dirCross;
    if(param < 0.f)
        return false;

    outIntersection = p2 + param * dir2;

    return Vector2f::dot(outIntersection - p1, dir1) >= 0.f;
}

//static
//dir should be normalized
bool GeometryUtils::lineLineSegmentIntersection( const Vector2f& p, const Vector2f& dir,
                                                 const Vector2f& p1, const Vector2f& p2, Vector2f &outIntersection)
{
    Vector2f segDir = (p2 - p1);
    float segDirLen = segDir.abs();
    if(segDirLen < EPSILON)
        return false;
    segDir = segDir / segDirLen;

    Vector2f dir90(-dir[1], dir[0]);
    float dirCross = Vector2f::dot(segDir, dir90);
    if(fabs(dirCross) < EPSILON)
        return false;
    float param = Vector2f::dot(p - p1, dir90) / dirCross;
    if(param < 0.f || param > segDirLen)
        return false;

    outIntersection = p1 + param * segDir;

    return true;
}

// static
bool GeometryUtils::rayPlaneIntersection( const Vector3f& crRayOrigin, const Vector3f& crRayDirection,
						  const Vector4f& crPlane,
						  Vector3f& rIntersectionPoint )
{
	Vector3f planeNormal = crPlane.xyz();
	float planeD = crPlane.w;

	float Vd = Vector3f::dot( planeNormal, crRayDirection );
	
	// TODO: epsilon
	if( abs( Vd ) > EPSILON )
	{
		float V0 = -Vector3f::dot( planeNormal, crRayOrigin ) + planeD; // TODO: negative? weird
		float t = V0 / Vd;
		if( t > 0 )
		{
			Vector3f scaledDirection( t * crRayDirection.normalized() );
			rIntersectionPoint = crRayOrigin + scaledDirection;
			return true;
		}
		return false;
	}
	return false;
}

// static
bool GeometryUtils::rayTriangleIntersection( const Vector3f& crRayOrigin, const Vector3f& crRayDirection,
		const Vector3f& crV0, const Vector3f& crV1, const Vector3f& crV2,
		float* t, float* u, float* v )
{
	Vector3f edge1;
	Vector3f edge2;
	Vector3f tvec;
	Vector3f pvec;
	Vector3f qvec;

	float det;
	float inv_det;

	// find vectors for two edges sharing vert0
	edge1 = crV1 - crV0;
	edge2 = crV2 - crV0;

	// begin calculating determinant - also used to calculate U parameter
	pvec = Vector3f::cross( crRayDirection, edge2 );
	det = Vector3f::dot( edge1, pvec );
	
#if 1
	if( det < EPSILON )
	{
		return false;
	}

	// calculate distance from vert0 to ray origin
	tvec = crRayOrigin - crV0;

	// calculate U parameter and test bounds
	*u = Vector3f::dot( tvec, pvec );
	if( *u < 0.0f || *u > det )
	{
		return false;
	}

	// prepare to test V parameter
	qvec = Vector3f::cross( tvec, edge1 );

	// calculate V parameter and test bounds
	*v = Vector3f::dot( crRayDirection, qvec );
	if( *v < 0.0 || ( *u + *v > det ) )
	{
		return false;
	}

	// calculate t, scale parameters, ray intersects triangle
	*t = Vector3f::dot( edge2, qvec );
	inv_det = 1.0f / det;
	*t *= inv_det;
	*u *= inv_det;
	*v *= inv_det;

#else
	// if determinant is near zero, ray lies in plane of triangle
	if( det > -EPSILON && det < EPSILON )
	{
		return false;
	}
    inv_det = 1.0f / det;

	// calculate distance from vert0 to ray origin
	Vector3f::subtract( pvRayOrigin, pV0, &tvec );

	// calculate U parameter and test bounds
	*u = inv_det * Vector3f::dot( &tvec, &pvec );
	if( *u < 0.0f || *u > 1.0f )
	{
		return false;
	}

	// prepare to test V parameter
	Vector3f::cross( &tvec, &edge1, &qvec );

	// calculate V parameter and test bounds
	*v = inv_det * Vector3f::dot( pvRayDirection, &qvec );
	if( *v < 0.0f || ( *u + *v > 1.0f ) )
	{
		return false;
	}

	// calculate t, ray intersects triangle
	*t = inv_det * Vector3f::dot( &edge2, &qvec );
#endif
	return true;
}

#if 0
// static
bool GeometryUtils::triangleAABBOverlap( Vector3f* pv0, Vector3f* pv1, Vector3f* pv2,
										BoundingBox3f* pBox )
{
	// use separating axis theorem to test overlap between triangle and box
	// need to test for overlap in these directions:
	// 1) the {x,y,z}-directions (actually, since we use the AABB of the triangle
	//    we do not even need to test these)
	// 2) normal of the triangle
	// 3) crossproduct(edge from tri, {x,y,z}-direction)
	//    this gives 3x3=9 more tests

	Vector3f v0;
	Vector3f v1;
	Vector3f v2;

	float min,max,p0,p1,p2,rad;

	float fex;
	float fey;
	float fez;

	Vector3f normal;
	Vector3f e0;
	Vector3f e1;
	Vector3f e2;

	Vector3f boxCenter;
	pBox->getCenter( &boxCenter );

	// move everything so that the box center is in (0,0,0)
	Vector3f::subtract( pv0, &boxCenter, &v0 );
	Vector3f::subtract( pv1, &boxCenter, &v1 );
	Vector3f::subtract( pv2, &boxCenter, &v2 );

	// compute triangle edges
	Vector3f::subtract( &v1, &v0, &e0 );
	Vector3f::subtract( &v2, &v1, &e1 );
	Vector3f::subtract( &v0, &v2, &e2 );

	// Bullet 3:
	// test the 9 tests first (this was faster)

	fex = fabs( e0[0] );
	fey = fabs( e0[1] );
	fez = fabs( e0[2] );

	AXISTEST_X01(e0[Z], e0[Y], fez, fey);

	AXISTEST_Y02(e0[Z], e0[X], fez, fex);

	AXISTEST_Z12(e0[Y], e0[X], fey, fex);



	fex = fabsf(e1[X]);

	fey = fabsf(e1[Y]);

	fez = fabsf(e1[Z]);

	AXISTEST_X01(e1[Z], e1[Y], fez, fey);

	AXISTEST_Y02(e1[Z], e1[X], fez, fex);

	AXISTEST_Z0(e1[Y], e1[X], fey, fex);



	fex = fabsf(e2[X]);

	fey = fabsf(e2[Y]);

	fez = fabsf(e2[Z]);

	AXISTEST_X2(e2[Z], e2[Y], fez, fey);

	AXISTEST_Y1(e2[Z], e2[X], fez, fex);

	AXISTEST_Z12(e2[Y], e2[X], fey, fex);



	/* Bullet 1: */

	/*  first test overlap in the {x,y,z}-directions */

	/*  find min, max of the triangle each direction, and test for overlap in */

	/*  that direction -- this is equivalent to testing a minimal AABB around */

	/*  the triangle against the AABB */



	/* test in X-direction */

	FINDMINMAX(v0[X],v1[X],v2[X],min,max);

	if(min>boxhalfsize[X] || max<-boxhalfsize[X]) return 0;



	/* test in Y-direction */

	FINDMINMAX(v0[Y],v1[Y],v2[Y],min,max);

	if(min>boxhalfsize[Y] || max<-boxhalfsize[Y]) return 0;



	/* test in Z-direction */

	FINDMINMAX(v0[Z],v1[Z],v2[Z],min,max);

	if(min>boxhalfsize[Z] || max<-boxhalfsize[Z]) return 0;



	/* Bullet 2: */

	/*  test if the box intersects the plane of the triangle */

	/*  compute plane equation of triangle: normal*x+d=0 */

	CROSS(normal,e0,e1);

	// -NJMP- (line removed here)

	if(!planeBoxOverlap(normal,v0,boxhalfsize)) return 0;	// -NJMP-



	return 1;   /* box and triangle overlaps */
}
#endif

// static
float GeometryUtils::triangleInterpolation( float interpolant0, float interpolant1, float interpolant2,
										   float u, float v )
{
	return interpolant0 + u * ( interpolant1 - interpolant0 ) + v * ( interpolant2 - interpolant0 );
}

// static
float GeometryUtils::pointToPlaneDistance( const Vector3f& point, const Vector4f& plane )
{
	// first find a point on the plane
	// we can just pick an intercept
	// for axis-aligned planes, pick another intercept until it works
	
	Vector3f pointOnPlane;

	if( plane[0] != 0 )
	{
		// x-intercept
		pointOnPlane = Vector3f( plane[3] / plane[0], 0, 0 );
	}
	else if( plane[1] != 0 )
	{
		pointOnPlane = Vector3f( 0, plane[3] / plane[1], 0 );
	}
	else
	{
		pointOnPlane = Vector3f( 0, 0, plane[3] / plane[1] );
	}

	Vector3f vectorToPoint = point - pointOnPlane;
	Vector3f unitNormal = plane.xyz().normalized();
	return Vector3f::dot( vectorToPoint, unitNormal );
}

float GeometryUtils::pointToLineDistance( const Vector3f& point, const Vector3f& linePoint, const Vector3f& lineDir )
{
    Vector3f diff = point - linePoint;
    float dot = Vector3f::dot(diff, lineDir); //assumes lineDir is normalized
    float distSq = diff.absSquared() - dot * dot;
    return (distSq > 0.f) ? sqrt(distSq) : 0.f;
}

float GeometryUtils::lineToLineDistance( const Vector3f& linePoint1, const Vector3f& lineDir1, const Vector3f& linePoint2, const Vector3f& lineDir2 )
{
    //distance is along the vector perpendicular to both lines
    Vector3f dirCross = Vector3f::cross(lineDir1, lineDir2);
    float crossLength = dirCross.abs();
    if(crossLength < EPSILON) //lines are approximately parallel
        return pointToLineDistance( linePoint1, linePoint2, lineDir2.normalized() );
    return fabs(Vector3f::dot(linePoint2 - linePoint1, dirCross) / crossLength);
}


#if 0
// static
void GeometryUtils::tripleSphereIntersection( Vector3f* c0, float r0,
									 Vector3f* c1, float r1,
									 Vector3f* c2, float r2,
									 int* numIntersections,
									 Vector3f* intersect0,
									 Vector3f* intersect1 )
{
	// compute the vector from the center of s0 --> s1
	Vector3f c0c1;
	Vector3f::subtract( c1, c0, &c0c1 );

	// distance between c0 and c1
	float d_c0c1 = c0c1.abs();
	// TODO: when does this not hold?
	float distanceToCenterOfCircle = ( d_c0c1 * d_c0c1 - r1 * r1 + r0 * r0 ) / ( 2 * d_c0c1 );

	Vector3f c0ToCenterOfCircle( &c0c1 );
	c0ToCenterOfCircle.normalize();
	c0ToCenterOfCircle.scale( distanceToCenterOfCircle );

	Vector3f centerOfCircle;
	Vector3f::add( c0, &c0ToCenterOfCircle, &centerOfCircle );

	// radius of the circle of intersection between s0 and s1
	float circleRadius = ( 1.0f / ( 2.0f * d_c0c1 ) ) * sqrt( 4 * d_c0c1 * d_c0c1 * r0 * r0 - ( d_c0c1 * d_c0c1 - r1 * r1 + r0 * r0 ) * ( d_c0c1 * d_c0c1 - r1 * r1 + r0 * r0 ) );

	// normal of plane
	Vector3f planeNormal( &c0c1 );
	planeNormal.normalize();

	// d for plane
	float planeD = Vector3f::dot( &centerOfCircle, &planeNormal );

	// compute intersection of plane with s2 to get second circle in plane
	float pseudoDistance = Vector3f::dot( &planeNormal, c2 ) - planeD;
	if( pseudoDistance < r2 )
	{
		// plane intersects sphere, center of circle is pseudodistance along -planeNormal from the center of the sphere
		Vector3f minusPseudoDistanceTimesNormal( &planeNormal );
		minusPseudoDistanceTimesNormal.scale( -pseudoDistance );

		Vector3f centerOfCircle2;
		Vector3f::add( c2, &minusPseudoDistanceTimesNormal, &centerOfCircle2 );

		float circleRadius2 = sqrt( r2 * r2 - pseudoDistance * pseudoDistance );
		
		Vector3f center1ToCenter2;
		Vector3f::subtract( &centerOfCircle2, &centerOfCircle, &center1ToCenter2 );
		float d_center1ToCenter2 = center1ToCenter2.abs();

		// TODO: when does this fail?
		// "d" from mathworld
		float distanceToCircleIntersection = ( d_center1ToCenter2 * d_center1ToCenter2 - circleRadius2 * circleRadius2 + circleRadius * circleRadius ) / ( 2.0f * d_center1ToCenter2 );
		Vector3f centerOfCircle1ToIntersection( &center1ToCenter2 );
		centerOfCircle1ToIntersection.normalize();
		centerOfCircle1ToIntersection.scale( distanceToCircleIntersection );

		Vector3f pointBetweenCircles;
		Vector3f::add( &centerOfCircle, &centerOfCircle1ToIntersection, &pointBetweenCircles );

		// "a / 2" from mathworld
		float distanceToActualPointFromPointBetweenCircles = ( 1.0f / ( 2.0f * d_center1ToCenter2 ) ) * sqrt( 4 * d_center1ToCenter2 * d_center1ToCenter2 * circleRadius * circleRadius - ( d_center1ToCenter2 * d_center1ToCenter2 - circleRadius2 * circleRadius2 + circleRadius * circleRadius ) * ( d_center1ToCenter2 * d_center1ToCenter2 - circleRadius2 * circleRadius2 + circleRadius * circleRadius ) );

		Vector3f vectorUpInPlane;
		Vector3f::cross( &center1ToCenter2, &planeNormal, &vectorUpInPlane );
		vectorUpInPlane.normalize();
		vectorUpInPlane.scale( distanceToActualPointFromPointBetweenCircles );

		Vector3f::add( &pointBetweenCircles, &vectorUpInPlane, intersect0 );
		vectorUpInPlane.negate();
		Vector3f::add( &pointBetweenCircles, &vectorUpInPlane, intersect1 );
	}
	else
	{
		*numIntersections = 1;
	}
}
#endif

// static
Vector3f GeometryUtils::randomPointInSphere( float radius, Random& random )
{
	float s = static_cast< float >( ( 2.f * random.nextDouble() ) - 1 );
	float phi = random.nextFloatRange( 0, 2.f * MathUtils::PI );

	float sqrtOneMinusSSquared = sqrt( 1.0f - s * s );
	float cosPhi = cos( phi );
	float sinPhi = sin( phi );

	return Vector3f
		(
			radius * sqrtOneMinusSSquared * cosPhi,
			radius * sqrtOneMinusSSquared * sinPhi,
			radius * s
		);
}

// static
std::vector< Vector2f > GeometryUtils::uniformSampleLineSegment( const Vector2f& p0, const Vector2f& p1, int nSamples )
{
	std::vector< Vector2f > samples( nSamples );

	Vector2f unit = ( p1 - p0 ).normalized();
	float sampleSpacing = ( p1 - p0 ).abs() / ( nSamples - 1 );
	for( int i = 0; i < nSamples; ++i )
	{
		samples[ i ] = p0 + i * sampleSpacing * unit;				
	}

	return samples;
}

// static
std::vector< Vector3f > GeometryUtils::uniformSampleLineSegment( const Vector3f& p0, const Vector3f& p1, int nSamples )
{
	std::vector< Vector3f > samples( nSamples );

	Vector3f unit = ( p1 - p0 ).normalized();
	float sampleSpacing = ( p1 - p0 ).abs() / ( nSamples - 1 );
	for( int i = 0; i < nSamples; ++i )
	{
		samples[ i ] = p0 + i * sampleSpacing * unit;				
	}

	return samples;
}

// static
std::vector< Vector2f > GeometryUtils::uniformSampleBoxAroundLineSegment( const Vector2f& p0, const Vector2f& p1,
	float width, int nSamplesWidth, int nSamplesLength )
{
	std::vector< Vector2f > samples( nSamplesWidth * nSamplesLength );

	Vector2f unitLength = ( p1 - p0 ).normalized();
	Vector2f unitWidth = unitLength.normal().normalized();
	float sampleSpacingW = width / ( nSamplesWidth - 1 );
	float sampleSpacingL = ( p1 - p0 ).abs() / ( nSamplesLength - 1 );

	Vector2f origin = p0 - 0.5f * width * unitWidth;
	for( int w = 0; w < nSamplesWidth; ++w )
	{
		for( int l = 0; l < nSamplesLength; ++l )
		{
			samples[ w * nSamplesLength + l ] =
				origin + w * sampleSpacingW * unitWidth + l * sampleSpacingL * unitLength;
		}
	}

	return samples;
}