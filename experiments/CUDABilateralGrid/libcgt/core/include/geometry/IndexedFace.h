#ifndef INDEXED_FACE_H
#define INDEXED_FACE_H

#include <vector>

class IndexedFace
{
public:

	IndexedFace();
	
	virtual ~IndexedFace();

	static bool isTriangle( IndexedFace* pFace );
	
	std::vector< int >* getPositionIndices();
	std::vector< int >* getTextureCoordinateIndices();
	std::vector< int >* getNormalIndices();

private:

	std::vector< int > m_vPositionIndices;
	std::vector< int > m_vTextureCoordinateIndices;
	std::vector< int > m_vNormalIndices;
};

#endif
