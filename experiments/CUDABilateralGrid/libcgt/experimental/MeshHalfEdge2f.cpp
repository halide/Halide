#include "MeshHalfEdge2f.h"

#include <cassert>
#include <cstdlib>

#include "MeshVertex2f.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

MeshHalfEdge2f::MeshHalfEdge2f( int sourceVertexIndex, int destinationVertexIndex ) :

	m_key( qMakePair( sourceVertexIndex, destinationVertexIndex ) ),
	m_pDestinationVertex( NULL ),
	m_pTwinHalfEdge( NULL ),
	m_pPreviousHalfEdge( NULL ),
	m_pNextHalfEdge( NULL ),
	m_pIncidentFace( NULL )

{
	// printf( "creating edge with key: %d, %d\n", sourceVertexIndex, destinationVertexIndex );
}

MeshHalfEdge2f::MeshHalfEdge2f( MeshHalfEdge2f* pCopy ) :

	m_key( pCopy->m_key ),

	m_pDestinationVertex( pCopy->m_pDestinationVertex ),
	m_pTwinHalfEdge( pCopy->m_pTwinHalfEdge ),
	m_pPreviousHalfEdge( pCopy->m_pPreviousHalfEdge ),
	m_pNextHalfEdge( pCopy->m_pNextHalfEdge ),
	m_pIncidentFace( pCopy->m_pIncidentFace )

{

}

QString MeshHalfEdge2f::toString()
{
	return QString( "< %1, %2 >" ).arg( m_key.first ).arg( m_key.second );
}

void MeshHalfEdge2f::setKey( int sourceVertexIndex, int destinationVertexIndex )
{
	// printf( "setting edge key to %d, %d\n", sourceVertexIndex, destinationVertexIndex );
	m_key = qMakePair( sourceVertexIndex, destinationVertexIndex );
}

MeshEdgeKey MeshHalfEdge2f::key()
{
	return m_key;
}

void MeshHalfEdge2f::setKey( MeshEdgeKey newKey )
{
	m_key = newKey;
}

bool MeshHalfEdge2f::isBoundary()
{
	return( twinHalfEdge() == NULL );
}

MeshVertex2f* MeshHalfEdge2f::destinationVertex()
{
	return m_pDestinationVertex;
}

void MeshHalfEdge2f::setDestinationVertex( MeshVertex2f* pv )
{
	m_pDestinationVertex = pv;
}

MeshHalfEdge2f* MeshHalfEdge2f::nextHalfEdgeOnFace()
{
	return m_pNextHalfEdge;
}

void MeshHalfEdge2f::setNextHalfEdgeOnFace( MeshHalfEdge2f* pe )
{
	m_pNextHalfEdge = pe;
}

MeshHalfEdge2f* MeshHalfEdge2f::previousHalfEdgeOnFace()
{
	return m_pPreviousHalfEdge;
}

void MeshHalfEdge2f::setPreviousHalfEdgeOnFace( MeshHalfEdge2f* pe )
{
	m_pPreviousHalfEdge = pe;
}

MeshHalfEdge2f* MeshHalfEdge2f::twinHalfEdge()
{
	return m_pTwinHalfEdge;
}

void MeshHalfEdge2f::setTwinHalfEdge( MeshHalfEdge2f* pe )
{
	m_pTwinHalfEdge = pe;
}

MeshFace2f* MeshHalfEdge2f::incidentFace()
{
	return m_pIncidentFace;
}

void MeshHalfEdge2f::setIncidentFace( MeshFace2f* pFace )
{
	m_pIncidentFace = pFace;
}

MeshVertex2f* MeshHalfEdge2f::sourceVertex()
{
	return previousHalfEdgeOnFace()->destinationVertex();
}

MeshFace2f* MeshHalfEdge2f::adjacentFace()
{
	return twinHalfEdge()->incidentFace();
}

MeshHalfEdge2f* MeshHalfEdge2f::nextNthHalfEdgeOnFace( int n )
{
	assert( n >= 0 );

	MeshHalfEdge2f* e = this;

	for( int i = 0; i < n; ++i )
	{
		e = e->nextHalfEdgeOnFace();
	}

	return e;
}

MeshHalfEdge2f* MeshHalfEdge2f::previousNthHalfEdgeOnFace( int n )
{
	assert( n >= 0 );

	MeshHalfEdge2f* e = this;

	for( int i = 0; i < n; ++i )
	{
		e = e->previousHalfEdgeOnFace();
	}

	return e;
}