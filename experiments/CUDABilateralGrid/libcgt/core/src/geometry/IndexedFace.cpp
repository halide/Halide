#include "geometry/IndexedFace.h"

using namespace std;

IndexedFace::IndexedFace()
{

}

IndexedFace::~IndexedFace()
{

}

// static
bool IndexedFace::isTriangle( IndexedFace* pFace )
{
	return( pFace->m_vPositionIndices.size() == 3 );
}

vector< int >* IndexedFace::getPositionIndices()
{
	return &m_vPositionIndices;
}

vector< int >* IndexedFace::getTextureCoordinateIndices()
{
	return &m_vTextureCoordinateIndices;
}

vector< int >* IndexedFace::getNormalIndices()
{
	return &m_vNormalIndices;
}
