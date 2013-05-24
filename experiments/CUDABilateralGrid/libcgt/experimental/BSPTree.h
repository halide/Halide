#ifndef BSP_TREE_H
#define BSP_TREE_H

#include <vector>

#include "geometry/Triangle3f.h"

class BSPTreeNode
{
public:

	BSPTreeNode( const Triangle3f& triangle );
	BSPTreeNode( const Triangle3f& triangle,
		BSPTreeNode* pNegativeChild,
		BSPTreeNode* pPositiveChild );

	Triangle3f getTriangle();
	BSPTreeNode* getNegativeChild();
	BSPTreeNode* getPositiveChild();

private:

	Triangle3f m_vTriangle;
	BSPTreeNode* m_pNegativeChild;
	BSPTreeNode* m_pPositiveChild;

};

class BSPTree
{
public:

	BSPTree( const std::vector< Triangle3f >& triangles );

	void backToFront( const Vector3f& direction,
		std::vector< Triangle3f >& triangles );

private:

	BSPTreeNode* m_pRootNode;

	BSPTreeNode* buildTree( std::vector< Triangle3f >& triangles,
		const std::vector< int >& overlappingIndices );

	void backToFrontHelper( const Vector3f& direction,
		BSPTreeNode* pCurrentNode,
		std::vector< Triangle3f >& triangles );
};

#endif // BSP_TREE_H
