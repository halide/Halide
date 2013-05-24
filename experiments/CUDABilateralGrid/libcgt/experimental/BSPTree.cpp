#include "geometry/BSPTree.h"

#include <cassert>

using namespace std;

BSPTreeNode::BSPTreeNode( const Triangle3f& triangle ) :

	m_vTriangle( triangle ),
	m_pNegativeChild( NULL ),
	m_pPositiveChild( NULL )

{

}

BSPTreeNode::BSPTreeNode( const Triangle3f& triangle,
						 BSPTreeNode* pNegativeChild,
						 BSPTreeNode* pPositiveChild ) :

	m_vTriangle( triangle ),
	m_pNegativeChild( pNegativeChild ),
	m_pPositiveChild( pPositiveChild )
{

}

Triangle3f BSPTreeNode::getTriangle()
{
	return m_vTriangle;
}

BSPTreeNode* BSPTreeNode::getNegativeChild()
{
	return m_pNegativeChild;
}

BSPTreeNode* BSPTreeNode::getPositiveChild()
{
	return m_pPositiveChild;
}

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

BSPTree::BSPTree( const vector< Triangle3f >& triangles )
{
	vector< Triangle3f > refinedTriangles( triangles );

	int nTriangles = triangles.size();
	vector< int > overlappingIndices( nTriangles );
	for( int i = 0; i < nTriangles; ++i )
	{
		overlappingIndices[ i ] = i;
	}

	m_pRootNode = buildTree( refinedTriangles, overlappingIndices );
}

void BSPTree::backToFront( const Vector3f& direction,
						  vector< Triangle3f >& triangles )
{
	backToFrontHelper( direction, m_pRootNode, triangles );
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

BSPTreeNode* BSPTree::buildTree( vector< Triangle3f >& triangles,
								const vector< int >& overlappingIndices )
{
	if( overlappingIndices.size() == 1 )
	{
		return new BSPTreeNode( triangles[ overlappingIndices[ 0 ] ] );
	}

	int splittingIndex = overlappingIndices[ 0 ]; // TODO: pick randomly?

	vector< int > negativeIndices; // triangles that lie in the left half-space
	vector< int > positiveIndices; // triangles that lie in the right half-space

	Vector4f splittingPlane = triangles[ splittingIndex ].plane();
	
	for( int i = 1; i < overlappingIndices.size(); ++i )
	{
		int index = overlappingIndices[ i ];
		Triangle3f pieces[ 3 ];
		int classifications[ 3 ];

		int classification = triangles[ index ].split( splittingPlane,
			pieces, classifications );

		if( classification == -1 )
		{
			negativeIndices.push_back( index );
		}
		else if( classification == 1 )
		{
			positiveIndices.push_back( index );
		}
		else if( classification == 3 ) // it split the triangle
		{
			int nTriangles = triangles.size();

			// so add the 3 new triangles to the list
			// the have indices:
			// { size, size + 1, size + 2 }
			triangles.push_back( pieces[ 0 ] );
			triangles.push_back( pieces[ 1 ] );
			triangles.push_back( pieces[ 2 ] );
			
			// classify them
			for( int j = 0; j < 3; ++j )
			{
				if( classifications[ j ] < 0 )
				{
					negativeIndices.push_back( nTriangles + j );
				}
				else
				{
					positiveIndices.push_back( nTriangles + j );
				}
			}
		}
		else
		{
			printf( "found 0!\n" );
			// assert( false );
		}
	}

	BSPTreeNode* pNegativeChild = NULL;
	BSPTreeNode* pPositiveChild = NULL;

	if( negativeIndices.size() > 0 )
	{
		pNegativeChild = buildTree( triangles, negativeIndices );
	}
	if( positiveIndices.size() > 0 )
	{
		pPositiveChild = buildTree( triangles, positiveIndices );
	}

	return new BSPTreeNode( triangles[ splittingIndex ], pNegativeChild, pPositiveChild );
}

void BSPTree::backToFrontHelper( const Vector3f& direction,
								BSPTreeNode* pCurrentNode,
								vector< Triangle3f >& triangles )
{
	Triangle3f currentTriangle = pCurrentNode->getTriangle();
	Vector3f currentNormal = currentTriangle.normal();

	
	float dotProduct = Vector3f::dot( currentNormal, direction );
	BSPTreeNode* pBackNode;
	BSPTreeNode* pFrontNode;

	if( dotProduct > 0 ) // positive halfspace is the back
	{
		pBackNode = pCurrentNode->getPositiveChild();
		pFrontNode = pCurrentNode->getNegativeChild();
	}
	else // negative halfspace is the back
	{
		pBackNode = pCurrentNode->getNegativeChild();
		pFrontNode = pCurrentNode->getPositiveChild();
	}

	if( pBackNode != NULL )
	{
		backToFrontHelper( direction, pBackNode, triangles );
	}

	triangles.push_back( currentTriangle );

	if( pFrontNode != NULL )
	{
		backToFrontHelper( direction, pFrontNode, triangles );
	}	
}
