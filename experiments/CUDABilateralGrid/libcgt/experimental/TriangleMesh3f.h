#ifndef TRIANGLE_MESH_3F
#define TRIANGLE_MESH_3F

#include <vector>

#include <common/BasicTypes.h>
#include <vecmath/Vector2f.h>
#include <vecmath/Vector3f.h>

// #include "../gl/includes.h"

#include "IndexedFace.h"
#include "BoundingBox3f.h"

// TODO: don't auto create buffer objects, let the user manage?
class TriangleMesh3f
{
public:

	// ========== Constructors ==========

	// Creates a triangle mesh from vectors passed in
	// Copies the data into an internal vector, no longer indexed
	// returns NULL on failure
	static TriangleMesh3f* create( std::vector< Vector3f >* pvVertices,
		std::vector< Vector2f >* pvTextureCoordinates,
		std::vector< Vector3f >* pvNormals,		
		std::vector< IndexedFace >* pvIndexedFaces );

	virtual ~TriangleMesh3f();


	// ========== Accessors ==========
	std::vector< Vector3f >* getVertices();
	std::vector< Vector2f >* getTextureCoordinates();
	std::vector< Vector3f >* getNormals();
	BoundingBox3f* getBoundingBox(); // returns the precomputed bounding box

	// ========== Drawing ==========
	void drawImmediate();
	void drawVertexBufferObject();

	// ========== Math ==========
	void recomputeBoundingBox(); // recomputes the bounding box


	/*
	bool checkIntegrity();

	std::vector< TriangleMesh3fFace* >* getFaces();
	std::vector< TriangleMesh3fVertex* >* getVertices();

	void recomputeTriangleStrips();

	void recomputeShadowVolume( Vector3f* pvLightPosition, ShadowVolume* pSV );

	// TODO: hitTest( Vector3f eye, Vector3f direction, Hit* hitList );
	*/

private:

	TriangleMesh3f();

	// ========== Triangle List Data ==========
	std::vector< Vector3f > m_vVertices;
	std::vector< Vector2f > m_vTextureCoordinates;
	std::vector< Vector3f > m_vNormals;

#if 0
	// ========== OpenGL Buffer Objects ==========
	GLBufferObject* m_pVertexBuffer;
	GLBufferObject* m_pTextureCoordinateBuffer;
	GLBufferObject* m_pNormalBuffer;
#endif

	// ========== Bounding Box ==========
	BoundingBox3f m_boundingBox;

	/*
	// Shadow Volume
	ShadowVolume m_shadowVolume;

	bool m_bHasTextureCoordinates;
	bool m_bHasNormals;

	*/
};

#endif
