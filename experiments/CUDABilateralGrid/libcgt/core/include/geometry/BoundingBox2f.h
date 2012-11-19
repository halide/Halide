#ifndef BOUNDING_BOX_2F
#define BOUNDING_BOX_2F

#include <vecmath/Vector2f.h>

class BoundingBox2f
{
public:

	// constructs an invalid bounding box with
	// min = numeric_limist< float >.max(),
	// max = numeric_limist< float >.lowest(),
	// so that merge( this, a ) = a
	BoundingBox2f();

	BoundingBox2f( float minX, float minY,
		float maxX, float maxY );
	BoundingBox2f( const Vector2f& min, const Vector2f& max );
	BoundingBox2f( const BoundingBox2f& rb );
	BoundingBox2f& operator = ( const BoundingBox2f& rb ); // assignment operator
	// no destructor necessary

	void print();

	Vector2f& minimum();
	Vector2f& maximum();

	Vector2f minimum() const;
	Vector2f maximum() const;
	
	Vector2f range() const;
	Vector2f center() const;
	
	bool intersectRay( const Vector2f& origin, const Vector2f& direction,
		float* tIntersect = nullptr );
	bool intersectLine( const Vector2f& p0, const Vector2f& p1 );

	// returns the smallest bounding box that contains both bounding boxes
	static BoundingBox2f merge( const BoundingBox2f& b0, const BoundingBox2f& b1 );

private:

	Vector2f m_min;
	Vector2f m_max;

	// TODO: if direction ~ 0, then it's parallel to that slab
	// TODO: early out: http://www.gamedev.net/topic/309689-ray-aabb-intersection/

	// intersects one axis of a ray against a "slab" (interval) defined by [s0,s1]
	// tEnter is updated if the new tEnter is bigger
	// tExit is updated if the new tExit is smaller
	void intersectSlab( float origin, float direction, float s0, float s1,
		float& tEnter, float& tExit );
};

#endif
