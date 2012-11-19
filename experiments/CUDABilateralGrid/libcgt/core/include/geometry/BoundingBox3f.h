#pragma once

#include <vecmath/Matrix4f.h>
#include <vecmath/Vector3f.h>
#include <vecmath/Vector4f.h>
#include <QString>
#include <QVector>
#include <vector>

class BoundingBox3f
{
public:

	// TODO: make a const INFINITY?

	// constructs an invalid bounding box with
	// min = numeric_limist< float >.max(),
	// max = numeric_limist< float >.lowest(),
	// so that merge( this, a ) = a
	BoundingBox3f();
	BoundingBox3f( float minX, float minY, float minZ,
		float maxX, float maxY, float maxZ );
	BoundingBox3f( const Vector3f& min, const Vector3f& max );
	BoundingBox3f( const BoundingBox3f& rb );
	BoundingBox3f& operator = ( const BoundingBox3f& rb ); // assignment operator
	// no destructor necessary	

	BoundingBox3f( const std::vector< Vector3f >& points, const Matrix4f& worldMatrix = Matrix4f::identity() );
	BoundingBox3f( const std::vector< Vector4f >& points, const Matrix4f& worldMatrix = Matrix4f::identity() );

	QString toString() const;

	Vector3f& minimum();
	Vector3f& maximum();

	Vector3f minimum() const;
	Vector3f maximum() const;

	Vector3f range() const;
	Vector3f center() const;
	
	float volume() const;

	// returns the minimum of the lengths of the 3 sides of this box
	float shortestSideLength() const;
	
	// returns the maximum of the lengths of the 3 sides of this box
	float longestSideLength() const;

	QVector< Vector3f > corners() const;

	// enlarges the box if p is outside it
	void enlarge( const Vector3f& p );

	// scales the box symmetrically about the center
	void scale( const Vector3f& s );

	// returns if this boundingbox overlaps the other bounding box
	// note that a overlaps b iff b overlaps a
	bool overlaps( const BoundingBox3f& other );

	bool intersectRay( const Vector3f& origin, const Vector3f& direction,
		float* tIntersect = nullptr );

	// returns the smallest bounding box that contains both bounding boxes
	static BoundingBox3f unite( const BoundingBox3f& b0, const BoundingBox3f& b1 );

    // returns the largest bounding box contained in both bounding boxes
    static BoundingBox3f intersect( const BoundingBox3f& b0, const BoundingBox3f& b1 );	

private:

	Vector3f m_min;
	Vector3f m_max;

	// TODO: if direction ~ 0, then it's parallel to that slab
	// TODO: early out: http://www.gamedev.net/topic/309689-ray-aabb-intersection/

	// intersects one axis of a ray against a "slab" (interval) defined by [s0,s1]
	// tEnter is updated if the new tEnter is bigger
	// tExit is updated if the new tExit is smaller
	void intersectSlab( float origin, float direction, float s0, float s1,
		float& tEnter, float& tExit );
};
