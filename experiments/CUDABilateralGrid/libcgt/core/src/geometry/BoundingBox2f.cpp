#include "geometry/BoundingBox2f.h"

#include <algorithm>
#include <cstdio>
#include <limits>

using namespace std;

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

BoundingBox2f::BoundingBox2f() :

	m_min( numeric_limits< float >::max(), numeric_limits< float >::max() ),
	m_max( numeric_limits< float >::lowest(), numeric_limits< float >::lowest() )

{

}

BoundingBox2f::BoundingBox2f( float minX, float minY, float maxX, float maxY ) :

	m_min( minX, minY ),
	m_max( maxX, maxY )

{

}

BoundingBox2f::BoundingBox2f( const Vector2f& min, const Vector2f& max ) :

	m_min( min ),
	m_max( max )

{

}

BoundingBox2f::BoundingBox2f( const BoundingBox2f& rb ) :

	m_min( rb.m_min ),
	m_max( rb.m_max )
	
{

}

BoundingBox2f& BoundingBox2f::operator = ( const BoundingBox2f& rb )
{
	if( this != &rb )
	{
		m_min = rb.m_min;
		m_max = rb.m_max;
	}
	return *this;
}

void BoundingBox2f::print()
{
	printf( "min: " );
	m_min.print();
	printf( "max: " );
	m_max.print();
}

Vector2f& BoundingBox2f::minimum()
{
	return m_min;
}

Vector2f& BoundingBox2f::maximum()
{
	return m_max;
}

Vector2f BoundingBox2f::minimum() const
{
	return m_min;
}

Vector2f BoundingBox2f::maximum() const
{
	return m_max;
}

Vector2f BoundingBox2f::range() const
{
	return( m_max - m_min );
}

Vector2f BoundingBox2f::center() const
{
	return( 0.5f * ( m_max + m_min ) );
}

bool BoundingBox2f::intersectRay( const Vector2f& origin, const Vector2f& direction,
	float* tIntersect )
{
	float tEnter = 0;
	float tExit = ( std::numeric_limits< float >::max )();

	intersectSlab( origin.x, direction.x, m_min.x, m_max.x, tEnter, tExit );
	intersectSlab( origin.y, direction.y, m_min.y, m_max.y, tEnter, tExit );

	bool intersected = ( tEnter < tExit );
	if( intersected && tIntersect != nullptr )
	{
		if( tEnter == 0 )
		{
			*tIntersect = tExit;
		}
		else
		{
			*tIntersect = std::min( tEnter, tExit );
		}
	}
	return intersected;
}

bool BoundingBox2f::intersectLine( const Vector2f& p0, const Vector2f& p1 )
{
	float tEnter = std::numeric_limits< float >::lowest();
	float tExit = ( std::numeric_limits< float >::max )();

	Vector2f direction = p1 - p0;

	intersectSlab( p0.x, direction.x, m_min.x, m_max.x, tEnter, tExit );
	intersectSlab( p1.y, direction.y, m_min.y, m_max.y, tEnter, tExit );

	bool intersected = ( tEnter < tExit );
	return intersected;
}

// static
BoundingBox2f BoundingBox2f::merge( const BoundingBox2f& b0, const BoundingBox2f& b1 )
{
	Vector2f b0Min = b0.minimum();
	Vector2f b0Max = b0.maximum();
	Vector2f b1Min = b1.minimum();
	Vector2f b1Max = b1.maximum();

	Vector2f mergedMin( std::min( b0Min.x, b1Min.x ), std::min( b0Min.y, b1Min.y ) );
	Vector2f mergedMax( std::max( b0Max.x, b1Max.x ), std::max( b0Max.y, b1Max.y ) );

	return BoundingBox2f( mergedMin, mergedMax );
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

void BoundingBox2f::intersectSlab( float origin, float direction, float s0, float s1,
	float& tEnter, float& tExit )
{
	float t0 = ( s0 - origin ) / direction;
	float t1 = ( s1 - origin ) / direction;

	if( t0 > t1 )
	{
		std::swap( t0, t1 );
	}

	if( t0 > tEnter )
	{
		tEnter = t0;
	}

	if( t1 < tExit )
	{
		tExit = t1;
	}
}
