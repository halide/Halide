#include "geometry/Plane3f.h"

#include "geometry/GeometryUtils.h"

// static
Plane3f Plane3f::XY()
{
	return Plane3f( 0, 0, 1, 0 );
}

// static
Plane3f Plane3f::YZ()
{
	return Plane3f( 1, 0, 0, 0 );
}

// static
Plane3f Plane3f::ZX()
{
	return Plane3f( 0, 1, 0, 0 );
}

Plane3f::Plane3f() :

	a( 0 ),
	b( 0 ),
	c( 0 ),
	d( 0 )

{

}

Plane3f::Plane3f( float _a, float _b, float _c, float _d ) :

	a( _a ),
	b( _b ),
	c( _c ),
	d( _d )

{

}

Plane3f::Plane3f( const Plane3f& p ) :

	a( p.a ),
	b( p.b ),
	c( p.c ),
	d( p.d )

{

}

Plane3f& Plane3f::operator = ( const Plane3f& p )
{
	if( this != &p )
	{
		a = p.a;
		b = p.b;
		c = p.c;
		d = p.d;
	}
	return *this;
}

Plane3f::Plane3f( const Vector3f& p0, const Vector3f& p1, const Vector3f& p2 )
{
    auto unitNormal = Vector3f::cross( p1 - p0, p2 - p0 ).normalized();
    d = -Vector3f::dot( unitNormal, p0 );
    a = unitNormal.x;
	b = unitNormal.y;
	c = unitNormal.z;
}

Plane3f::Plane3f( const Vector3f& p, const Vector3f& normal )
{
    auto unitNormal = normal.normalized();
    d = -Vector3f::dot( unitNormal, p );
	a = unitNormal.x;
	b = unitNormal.y;
	c = unitNormal.z;
}

Vector3f Plane3f::normal() const
{
	return Vector3f( a, b, c );
}

Vector3f Plane3f::unitNormal() const
{
	return normal().normalized();
}

Vector3f Plane3f::closestPointOnPlane( const Vector3f& p ) const
{
    float d = distance( p );
    return ( p - d * unitNormal() );
}

float Plane3f::distance( const Vector3f& p ) const
{
    // pick a point x on the plane
    // get the vector x --> p
    // distance is the projection on the unit normal
    // xp dot unitNormal
    auto x = pointOnPlane();
    auto xp = p - x;
    return Vector3f::dot( xp, unitNormal() );
}

Vector3f Plane3f::pointOnPlane() const
{
    float den = a * a + b * b + c * c;
    return Vector3f( -a * d / den, -b * d / den, -c * d / den );
}

Matrix3f Plane3f::basis( const Vector3f& u ) const
{
    // normalize u first
    auto u2 = u.normalized();
    auto n = unitNormal();

    // u is the vector triple product: ( n x u ) x n
    //
    // u is the preferred direction, which may be be in the plane
    // we want u to be the projection of u in the plane
    // and v to be perpendicular to both
    // but v is n cross u regardless of whether u is in the plane or not
    // so compute v, then cross v with n to get u in the plane
    auto v = Vector3f::cross( n, u ).normalized();
    u2 = Vector3f::cross( v, n ).normalized();

    return Matrix3f( u2, v, n );
}

Matrix3f Plane3f::basis() const
{
    auto n = unitNormal();

	Vector3f u;
	Vector3f v;
	GeometryUtils::getBasis( n, &u, &v );

    return Matrix3f( u, v, n );
}

Plane3f Plane3f::flipped() const
{
    return Plane3f( -a, -b, -c, -d );
}

Plane3f Plane3f::offset( float z ) const
{
    return Plane3f( a, b, c, d - z * ( a + b + c ) );
}

bool Plane3f::intersectRay( const Vector3f& origin, const Vector3f& direction,
	float* tIntersect )
{
    Vector3f u = unitNormal();
    float vd = Vector3f::dot( u, direction );
            
    // ray is parallel to plane
    if( vd == 0 )
    {
        return false;
    }

    float v0 = -( Vector3f::dot( u, origin ) + d );
    float t = v0 / vd;

	if( tIntersect != nullptr )
	{
		*tIntersect = t;
	}
	return( t > 0 );
}

// static
float Plane3f::cosineDihedralAngle( const Plane3f& p0, const Plane3f& p1 )
{
    return Vector3f::dot( p0.unitNormal(), p1.unitNormal() );
}