#include "geometry/LineIntersection.h"

//////////////////////////////////////////////////////////////////////////
// Ptqlic
//////////////////////////////////////////////////////////////////////////

// static
LineIntersection::IntersectionResult LineIntersection::lineLineIntersection( const Vector2f& p0, const Vector2f& p1,
																			const Vector2f& q0, const Vector2f& q1,
																			float* tp, float* tq )
{
	float denom = ( q1.y - q0.y ) * ( p1.x - p0.x ) - ( q1.x - q0.x ) * ( p1.y - p0.y );
	float nume_a = ( q1.x - q0.x ) * ( p0.y - q0.y ) - ( q1.y - q0.y ) * ( p0.x - q0.x );
	float nume_b = ( p1.x - p0.x ) * ( p0.y - q0.y ) - ( p1.y - p0.y ) * ( p0.x - q0.x );

	if( denom == 0.0f )
	{
		if( nume_a == 0.0f && nume_b == 0.0f )
		{
			return COINCIDENT;
		}
		return PARALLEL;
	}

	*tp = nume_a / denom;
	*tq = nume_b / denom;

	return INTERESECTING;
}

// static
LineIntersection::IntersectionResult LineIntersection::segmentSegmentIntersection( const Vector2f& p0, const Vector2f& p1,
																				  const Vector2f& q0, const Vector2f& q1,
																				  Vector2f* intersectionPoint )
{
	float tp;
	float tq;

	IntersectionResult result = lineLineIntersection( p0, p1, q0, q1, &tp, &tq );
	if( result == INTERESECTING )
	{
		if( tp >= 0.0f && tp <= 1.0f && tq >= 0.0f && tq <= 1.0f )
		{
			// get the intersection point
			intersectionPoint->x = p0.x + tp * ( p1.x - p0.x );
			intersectionPoint->y = p0.y + tp * ( p1.y - p0.y );

			return INTERESECTING;
		}
	}
	return NOT_INTERESECTING;
}

// static
LineIntersection::IntersectionResult LineIntersection::raySegmentIntersection( const Vector2f& rayOrigin, const Vector2f& rayDirection,
																			  const Vector2f& segmentBegin, const Vector2f& segmentEnd,
																			  Vector2f* intersectionPoint )
{
	float tp;
	float tq;

	// first intersect the lines
	Vector2f p0 = rayOrigin;
	Vector2f p1 = rayOrigin + rayDirection; // find two points on the ray
	Vector2f q0 = segmentBegin;
	Vector2f q1 = segmentEnd;

	IntersectionResult result = lineLineIntersection( p0, p1, q0, q1, &tp, &tq );
	if( result == INTERESECTING )
	{
		if( tq >= 0.f && tq <= 1.f && tp > 0.f )
		{
			intersectionPoint->x = q0.x + tq * ( q1.x - q0.x );
			intersectionPoint->y = q0.y + tq * ( q1.y - q0.y );
			return INTERESECTING;
		}
	}
	return NOT_INTERESECTING;
}
