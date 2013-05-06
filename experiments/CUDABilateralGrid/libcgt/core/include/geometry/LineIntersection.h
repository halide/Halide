#ifndef LINE_INTERSECTION_H
#define LINE_INTERSECTION_H

#include "vecmath/Vector2f.h"

// Slightly modified copy of the code found here:
// http://local.wasp.uwa.edu.au/~pbourke/geometry/lineline2d/

class LineIntersection
{
public:

	enum IntersectionResult
	{
		PARALLEL,
		COINCIDENT,
		NOT_INTERESECTING,
		INTERESECTING
	};

	// computes the intersection between lines p0 --> p1 and q0 --> q1
	// returns PARALLEL, COINCIDENT, or INTERSECTING
	// if they are INTERSECTING, tp contains the distance along p0 --> p1 (isectpoint = p0 + tp * ( p1 - p0 ) )
	// similarly for tq
	static IntersectionResult lineLineIntersection( const Vector2f& p0, const Vector2f& p1,
		const Vector2f& q0, const Vector2f& q1,
		float* tp, float* tq );

	static IntersectionResult segmentSegmentIntersection( const Vector2f& p0, const Vector2f& p1,
		const Vector2f& q0, const Vector2f& q1,
		Vector2f* intersectionPoint );

	static IntersectionResult raySegmentIntersection( const Vector2f& rayOrigin, const Vector2f& rayDirection,
		const Vector2f& segmentBegin, const Vector2f& segmentEnd,
		Vector2f* intersectionPoint );

private:


};

#endif // LINE_INTERSECTION_H
