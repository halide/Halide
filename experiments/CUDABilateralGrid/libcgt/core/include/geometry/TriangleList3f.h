#ifndef TRIANGLE_LIST_3F_H
#define TRIANGLE_LIST_3F_H

#include <vector>

#include "BoundingBox3f.h"

class IndexedFace;
class Vector2f;
class Vector3f;

class TriangleList3f
{
public:

	// ========== Constructors ==========

	static TriangleList3f* create( std::vector< Vector3f >* pvPositions,
		std::vector< Vector2f >* pvTextureCoordinates,
		std::vector< Vector3f >* pvNormals,
		std::vector< IndexedFace >* pvIndexedFaces );

	// TODO: read in N sets of texture coordinates
	// TODO: allow multiple sets
	static TriangleList3f* create( const char* filename );
	
	virtual ~TriangleList3f(); // destructor

	// ========== I/O ==========
	virtual bool serialize( const char* filename );

	// ========== Geometry ==========
	void recomputeBoundingBox();
	BoundingBox3f* getBoundingBox();

	int getNumVertices();

	float* getPositions();

	bool hasTextureCoordinates();
	float* getTextureCoordinates();

	bool hasNormals();
	float* getNormals();

	// intersects a ray with this triangle list
	// returns true if there exists an intersection
	// *index contains the index of the VERTEX
	// to get the position, multiply by 3
	// to get the texture coordinate, multiply by 2
	// intersection point is at *u * edge01 + *v * edge02
	bool rayIntersection( const Vector3f& crRayOrigin, const Vector3f& crRayDirection,		
		int* index,
		float* t,
		float* u, float* v );

	// TODO: attachAttributes( vector* );
	// TODO: attachTextureCoordinates

	// TOOD: look at this
	// ========== OpenGL Drawing ==========

	// GLDrawable* createStaticDrawable();

	// TODO:
	// void updateStreamingGLDrawable( GLDrawable* pgd );

protected:

	float* m_afPositions;
	float* m_afTextureCoordinates;
	float* m_afNormals;

	int m_iNumVertices;

	BoundingBox3f m_boundingBox;

private:

	TriangleList3f( float* afPositions, float* afTextureCoordinates, float* afNormals,
		int numVertices );

	// determines if the input data has texture coordinates and normals
	static void determineAttributes( std::vector< IndexedFace >* pvIndexedFaces,
		bool* pbHasTextureCoordinates, bool* pbHasNormals );

	// counts the number of vertices needed in a triangle list to triangulate the data
	static int countTotalNumVertices( std::vector< IndexedFace >* pvIndexedFaces );

	// triangulates the input data and populates the float arrays passed in
	static void populateDataArrays( std::vector< Vector3f >* pvPositions, 
		std::vector< Vector2f >* pvTextureCoordinates,
		std::vector< Vector3f >* pvNormals,
		std::vector< IndexedFace >* pvIndexedFaces,
		bool bHasTextureCoordinates, bool bHasNormals,
		float* afPositions, float* afTextureCoordinates, float* afNormals );
};

#endif
